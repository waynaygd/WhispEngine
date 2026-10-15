FetchContent_Declare(lua URL https://www.lua.org/ftp/lua-5.4.8.tar.gz
  URL_HASH SHA256=4f18ddae154e793e46eeab727c59ef1c0c0c2b744e7b94219710d76f530629ae)
FetchContent_MakeAvailable(lua)
file(GLOB LUA_SOURCES CONFIGURE_DEPENDS "${lua_SOURCE_DIR}/src/*.c")
list(REMOVE_ITEM LUA_SOURCES "${lua_SOURCE_DIR}/src/lua.c" "${lua_SOURCE_DIR}/src/luac.c")
add_library(WhispLua STATIC ${LUA_SOURCES})
target_include_directories(WhispLua PUBLIC "${lua_SOURCE_DIR}/src")
# sol2 v3.3.1, pinned to its immutable commit.
FetchContent_Declare(sol2 GIT_REPOSITORY https://github.com/ThePhD/sol2.git GIT_TAG dca62a0f02bb45f3de296de3ce00b1275eb34c25)
FetchContent_MakeAvailable(sol2)
target_compile_definitions(sol2 INTERFACE SOL_ALL_SAFETIES_ON=1)
