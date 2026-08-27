# components/ports/CMakeLists.txt
# 组件名取 "ports"(六边形架构的端口),避免与 ESP-IDF 内置 "hal" 组件重名冲突。
# 头文件命名空间仍为 hal/,业务层 #include "hal/xxx.h" 不变。
idf_component_register(INCLUDE_DIRS include)   # 纯接口,无 SRCS,无 REQUIRES
