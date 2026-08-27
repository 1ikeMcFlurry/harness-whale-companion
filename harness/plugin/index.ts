import dgram from 'node:dgram'
import { spawn, type ChildProcess } from 'node:child_process'
import { randomUUID } from 'node:crypto'
import { existsSync } from 'node:fs'
import { homedir } from 'node:os'
import { join } from 'node:path'

export const name = 'harness-whale-companion'
export const inject = ['commands', 'tools', 'sessionTitle']

const PORT = Number.parseInt(process.env.HARNESS_WHALE_UDP_PORT || '8765', 10)
const TELEMETRY_PORT = Number.parseInt(process.env.HARNESS_WHALE_TELEMETRY_PORT || '8766', 10)
const HOST = '127.0.0.1'

type Status = {
  v: 1
  state: 'offline' | 'idle' | 'thinking' | 'tool' | 'waiting' | 'done' | 'error' | 'stopped' | 'question'
  tool: string
  elapsed: number
  todoDone: number
  todoTotal: number
  newTurn?: boolean
  title: string
  sentAt: number
}

type HardwareStatus = {
  connected: boolean
  state: string
  device: string
  mtu?: number
  packetsSent?: number
  error?: string
  receivedAt: number
}

function eventType(event: any): string {
  return String(event?.type || event?.event || event?.name || '')
}

function eventBody(event: any): any {
  if (event?.data && typeof event.data === 'object') return event.data
  return event?.payload && typeof event.payload === 'object' ? event.payload : event
}

function sessionKey(session: any): string {
  return String(session?.header?.id || session?.id || session?.sessionId || session?.key || 'default')
}

function latestSessionTitle(ctx: any, session: any, eventTitle?: unknown): string {
  const fromEvent = typeof eventTitle === 'string' ? eventTitle.trim() : ''
  if (fromEvent) return fromEvent
  try {
    return String(ctx.sessionTitle?.get?.(session)?.title || '').trim()
  } catch {
    return ''
  }
}

function toolName(body: any): string {
  return String(body?.name || body?.tool?.name || body?.call?.name || body?.tool || body?.toolName || 'other')
}

function reasonKind(value: any): string {
  return String(value?.kind || value || '').trim().toLowerCase()
}

function isQuestionTool(name: string): boolean {
  return name.trim().toLowerCase().replace(/[.\/-]/g, '_') === 'ask_user_question'
}

type QuestionOption = { label: string; description?: string }
type HardwareQuestion = {
  id: string
  question: string
  options?: QuestionOption[]
  multi_select?: boolean
}

export function hardwareQuestions(value: any): HardwareQuestion[] | null {
  const questions = value?.questions
  if (!Array.isArray(questions) || questions.length === 0) return null
  for (const question of questions) {
    if (!question || typeof question.id !== 'string' || !Array.isArray(question.options) ||
        question.multi_select === true || question.options.length < 2 ||
        question.options.some((option: any) => !option || typeof option.label !== 'string')) {
      return null
    }
  }
  return questions as HardwareQuestion[]
}

function askOneOnHardware(question: HardwareQuestion, signal: AbortSignal): Promise<string> {
  const requestId = randomUUID()
  const offered = question.options!.map(option => option.label)
  // Four labels fit in one firmware frame. Other sizes also expose the web-only
  // custom-answer escape hatch; the bridge pages longer lists automatically.
  const options = offered.length === 4 ? offered : [...offered, '其他(电脑)']
  const customIndex = options.length > offered.length ? offered.length : -1
  const client = dgram.createSocket('udp4')
  return new Promise((resolve, reject) => {
    let settled = false
    const finish = (error?: Error, selected?: string) => {
      if (settled) return
      settled = true
      signal.removeEventListener('abort', onAbort)
      client.close()
      if (error) reject(error)
      else resolve(selected!)
    }
    const cancel = () => {
      const document = Buffer.from(JSON.stringify({ v: 1, kind: 'harness-question-cancel', requestId }))
      try { client.send(document, PORT, HOST) } catch { /* socket may already be closing */ }
    }
    const onAbort = () => {
      cancel()
      finish(new Error('hardware question cancelled'))
    }
    signal.addEventListener('abort', onAbort, { once: true })
    client.on('error', error => finish(error))
    client.on('message', (data, info) => {
      if (info.address !== '127.0.0.1' && info.address !== '::1') return
      try {
        const answer = JSON.parse(data.toString('utf8'))
        if (answer?.v !== 1 || answer?.kind !== 'harness-answer' ||
            answer?.requestId !== requestId || !Number.isInteger(answer?.selected)) return
        const index = Number(answer.selected)
        if (index === customIndex) {
          finish(new Error('custom answer requires the computer'))
        } else if (index >= 0 && index < offered.length) {
          finish(undefined, offered[index])
        }
      } catch {
        // Ignore malformed loopback replies.
      }
    })
    client.bind(0, HOST, () => {
      if (signal.aborted) return onAbort()
      const document = Buffer.from(JSON.stringify({
        v: 1, kind: 'harness-question', requestId,
        question: question.question,
        options,
      }))
      client.send(document, PORT, HOST)
    })
  })
}

async function askOnHardware(questions: HardwareQuestion[], signal: AbortSignal) {
  const answers = []
  for (const question of questions) {
    const selected = await askOneOnHardware(question, signal)
    answers.push({ id: question.id, selected: [selected] })
  }
  const value = { answers }
  return {
    content: [{ type: 'text', text: JSON.stringify(value) }],
    isError: false,
    value,
  }
}

export function apply(ctx: any) {
  const socket = dgram.createSocket('udp4')
  const telemetrySocket = dgram.createSocket({ type: 'udp4', reuseAddr: true })
  const states = new Map<string, Status & { startedAt?: number }>()
  const finalTimers = new Set<ReturnType<typeof setTimeout>>()
  const balanceRefreshTimers = new Set<ReturnType<typeof setTimeout>>()
  let activeKey = 'agent'
  let bridgeChild: ChildProcess | null = null
  let hardware: HardwareStatus = {
    connected: false,
    state: 'bridge-offline',
    device: 'HARNESS-WHALE',
    receivedAt: 0,
  }

  telemetrySocket.on('message', (data: Buffer, info: dgram.RemoteInfo) => {
    if (info.address !== '127.0.0.1' && info.address !== '::1') return
    try {
      const message = JSON.parse(data.toString('utf8'))
      if (message?.v !== 1 || message?.kind !== 'harness-whale-hardware') return
      const next: HardwareStatus = {
        connected: Boolean(message.connected),
        state: String(message.state || 'unknown'),
        device: String(message.device || 'HARNESS-WHALE'),
        mtu: Number.isFinite(message.mtu) ? Number(message.mtu) : undefined,
        packetsSent: Number.isFinite(message.packetsSent) ? Number(message.packetsSent) : undefined,
        error: message.error ? String(message.error) : undefined,
        receivedAt: Date.now(),
      }
      if (next.connected !== hardware.connected || next.state !== hardware.state) {
        console.info(`[harness-whale] hardware ${next.connected ? 'connected' : next.state}`)
      }
      hardware = next
    } catch {
      // Telemetry is loopback-only and non-authoritative; ignore malformed frames.
    }
  })
  telemetrySocket.on('error', (error: Error) => {
    console.warn(`[harness-whale] telemetry unavailable: ${error.name}`)
  })
  telemetrySocket.bind(TELEMETRY_PORT, HOST)

  const startManagedBridge = () => {
    if (bridgeChild || (hardware.receivedAt > 0 && Date.now() - hardware.receivedAt < 7000)) return
    const script = process.env.HARNESS_WHALE_BRIDGE_SCRIPT ||
      join(homedir(), '.dsh', 'harness-whale-bridge', 'harness_ble_bridge.py')
    const python = process.env.HARNESS_WHALE_PYTHON || (process.platform === 'win32'
      ? join(homedir(), '.dsh', 'harness-whale-bridge', '.venv', 'Scripts', 'python.exe')
      : join(homedir(), '.dsh', 'harness-whale-bridge', '.venv', 'bin', 'python'))
    if (!existsSync(script) || !existsSync(python)) return
    console.info('[harness-whale] starting managed BLE bridge')
    bridgeChild = spawn(python, [script], {
      cwd: join(homedir(), '.dsh', 'passport-bridge'),
      env: process.env,
      stdio: ['ignore', 'ignore', 'pipe'],
      windowsHide: true,
    })
    bridgeChild.stderr?.setEncoding('utf8')
    bridgeChild.stderr?.on('data', (chunk: string) => {
      for (const line of chunk.split(/\r?\n/).map(value => value.trim()).filter(Boolean)) {
        console.info(`[harness-whale-bridge] ${line}`)
      }
    })
    bridgeChild.once('exit', () => { bridgeChild = null })
  }
  const bridgeGuard = setInterval(startManagedBridge, 5000)
  const bridgeStartTimer = setTimeout(startManagedBridge, 2500)

  ctx.on('tools/execute', async (exec: any, next: () => Promise<any>) => {
    if (!isQuestionTool(String(exec?.name || ''))) return next()
    const questions = hardwareQuestions(exec?.arguments)
    if (questions === null) return next()

    const downstream = new AbortController()
    const hardware = new AbortController()
    const originalSignal: AbortSignal = exec.signal
    const abortBoth = () => {
      downstream.abort(originalSignal.reason)
      hardware.abort(originalSignal.reason)
    }
    originalSignal.addEventListener('abort', abortBoth, { once: true })
    exec.signal = downstream.signal

    const webResult = next().then(
      value => ({ source: 'web' as const, value }),
      error => ({ source: 'web-error' as const, error }),
    )
    const hardwareResult = askOnHardware(questions, hardware.signal).then(
      value => ({ source: 'hardware' as const, value }),
      error => ({ source: 'hardware-error' as const, error }),
    )

    try {
      const first = await Promise.race([webResult, hardwareResult])
      if (first.source === 'hardware') {
        downstream.abort('answered on HARNESS-WHALE')
        await webResult
        return first.value
      }
      if (first.source === 'web') {
        hardware.abort('answered on computer')
        return first.value
      }
      if (first.source === 'hardware-error') {
        const web = await webResult
        if (web.source === 'web') return web.value
        throw web.error
      }
      hardware.abort('computer question failed')
      throw first.error
    } finally {
      originalSignal.removeEventListener('abort', abortBoth)
      hardware.abort('question settled')
    }
  }, { global: true })

  ctx.commands.register({
    name: 'whale',
    description: 'show the Harness Whale companion hardware link',
    handler: () => {
      const now = Date.now()
      const bridgeOnline = hardware.receivedAt > 0 && now - hardware.receivedAt < 7000
      const linked = bridgeOnline && hardware.connected
      const agent = states.get(activeKey) || states.get('agent')
      const lastSeen = hardware.receivedAt > 0
        ? `${Math.max(0, Math.floor((now - hardware.receivedAt) / 1000))}s ago`
        : 'never'
      return {
        kind: linked ? 'success' : 'error',
        text: [
          'Harness Whale companion',
          `Hardware: ${linked ? 'connected' : 'disconnected'}`,
          `Bridge: ${bridgeOnline ? hardware.state : 'offline'}`,
          `Device: ${hardware.device}`,
          `Last heartbeat: ${lastSeen}`,
          `Packets: ${hardware.packetsSent ?? 0}`,
          `Agent mirror: ${agent?.state || 'idle'} / ${agent?.tool || 'none'}`,
          `Task title: ${agent?.title || 'waiting for title'}`,
          ...(hardware.error ? [`Last reconnect reason: ${hardware.error}`] : []),
        ].join('\n'),
      }
    },
  })

  const emit = (key: string, patch: Partial<Status> = {}) => {
    const previous = states.get(key)
    const startedAt = patch.newTurn ? Date.now() : previous?.startedAt
    const status: Status & { startedAt?: number } = {
      v: 1,
      state: previous?.state || 'idle',
      tool: previous?.tool || 'none',
      elapsed: startedAt ? Math.max(0, Math.floor((Date.now() - startedAt) / 1000)) : (previous?.elapsed || 0),
      todoDone: previous?.todoDone || 0,
      todoTotal: previous?.todoTotal || 0,
      title: previous?.title || '',
      sentAt: Date.now(),
      ...patch,
      startedAt,
    }
    states.set(key, status)
    const { startedAt: _private, ...publicStatus } = status
    socket.send(Buffer.from(JSON.stringify(publicStatus)), PORT, HOST)
  }

  const sendBalanceRefresh = () => {
    socket.send(Buffer.from(JSON.stringify({
      v: 1,
      kind: 'harness-balance-refresh',
      sentAt: Date.now(),
    })), PORT, HOST)
  }

  const requestBalanceRefreshBurst = (reason: string) => {
    for (const timer of balanceRefreshTimers) clearTimeout(timer)
    balanceRefreshTimers.clear()
    sendBalanceRefresh()
    for (const delay of [3000, 10000, 30000]) {
      const timer = setTimeout(() => {
        balanceRefreshTimers.delete(timer)
        sendBalanceRefresh()
      }, delay)
      balanceRefreshTimers.add(timer)
    }
    console.info(`[harness-whale] balance refresh scheduled (${reason})`)
  }

  emit('agent', { state: 'idle', tool: 'none' })
  const heartbeat = setInterval(() => emit(activeKey), 2000)

  ctx.on('session/created', (session: any) => {
    const title = latestSessionTitle(ctx, session)
    if (!title) return
    const key = sessionKey(session)
    activeKey = key
    emit(key, { title, newTurn: false })
  }, { global: true })

  ctx.on('agent/status', (payload: any) => {
    const raw = String(payload?.status || payload?.state || payload || '').toLowerCase()
    const agentSession = payload?.agent?.session
    const key = agentSession ? sessionKey(agentSession) : activeKey
    activeKey = key
    if (raw === 'running') {
      const previous = states.get(key)
      const beginsTask = !previous?.startedAt || ['idle', 'done', 'error', 'stopped', 'offline'].includes(
        previous.state,
      )
      emit(key, { state: 'thinking', tool: 'none', newTurn: beginsTask })
    }
    if (raw === 'idle') {
      const previousState = states.get(key)?.state || ''
      if (['thinking', 'tool', 'waiting', 'question'].includes(previousState)) {
        requestBalanceRefreshBurst('agent idle fallback')
      }
      if (!['waiting', 'question', 'done', 'error', 'stopped'].includes(previousState)) {
        emit(key, { state: 'idle', tool: 'none' })
      }
    }
  }, { global: true })

  ctx.on('session/event', (session: any, event: any) => {
    const key = sessionKey(session)
    activeKey = key
    const type = eventType(event)
    const body = eventBody(event)
    const observedTitle = latestSessionTitle(
      ctx,
      session,
      type === 'session/title' ? body?.title : undefined,
    )
    if (observedTitle && observedTitle !== states.get(key)?.title) {
      emit(key, { title: observedTitle, newTurn: false })
    }
    switch (type) {
      case 'turn/start':
        emit(key, {
          state: 'thinking', tool: 'none', elapsed: 0, newTurn: true,
          title: observedTitle || states.get(key)?.title || '',
        })
        break
      case 'session/title':
        emit(key, { title: observedTitle, newTurn: false })
        break
      case 'tool/call': {
        const tool = toolName(body)
        emit(key, {
          state: isQuestionTool(tool) ? 'question' : 'tool',
          tool: isQuestionTool(tool) ? 'none' : tool,
          newTurn: false,
        })
        break
      }
      case 'tool/result':
        emit(key, { state: body?.error ? 'error' : 'thinking', tool: 'none', newTurn: false })
        break
      case 'todo/write': {
        const todos = Array.isArray(body?.todos) ? body.todos : []
        const done = todos.filter((todo: any) => ['done', 'completed', 'complete'].includes(
          String(todo?.status || '').toLowerCase())).length
        emit(key, { todoDone: done, todoTotal: todos.length, newTurn: false })
        break
      }
      case 'approval/asked':
        emit(key, { state: 'waiting', tool: 'none', newTurn: false })
        break
      case 'approval/decided':
        emit(key, { state: 'thinking', tool: 'none', newTurn: false })
        break
      case 'question/requested':
        emit(key, { state: 'question', tool: 'none', newTurn: false })
        break
      case 'question/answered':
      case 'question/resolved':
      case 'question/cancelled':
        emit(key, { state: 'thinking', tool: 'none', newTurn: false })
        break
      case 'turn/end': {
        const reason = body?.reason
        const kind = reasonKind(reason)
        const cancelKind = reasonKind(reason?.reason)
        const stopped = (kind === 'aborted' && cancelKind === 'user') ||
          ['cancelled', 'canceled', 'stopped'].includes(kind)
        const failed = Boolean(body?.error) ||
          ['error', 'blocked', 'interrupted', 'max-tokens'].includes(kind) ||
          (kind === 'aborted' && !stopped)
        const finalState = stopped ? 'stopped' : failed ? 'error' : 'done'
        emit(key, { state: finalState, tool: 'none', newTurn: false })
        requestBalanceRefreshBurst('turn end')
        const timer = setTimeout(() => {
          finalTimers.delete(timer)
          if (states.get(key)?.state === finalState) emit(key, { state: 'idle', tool: 'none' })
        }, 5000)
        finalTimers.add(timer)
        break
      }
    }
  }, { global: true })

  ctx.effect(() => () => {
    clearInterval(heartbeat)
    clearInterval(bridgeGuard)
    clearTimeout(bridgeStartTimer)
    for (const timer of finalTimers) clearTimeout(timer)
    for (const timer of balanceRefreshTimers) clearTimeout(timer)
    if (bridgeChild && !bridgeChild.killed) bridgeChild.kill()
    socket.close()
    telemetrySocket.close()
  })
}
