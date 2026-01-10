function(PickSourceFiles current_dir variable)
    # 1. 首先检查当前目录是否在排除列表中 (支持目录排除)
    list(FIND ARGN "${current_dir}" IS_DIR_EXCLUDED)
    if (NOT IS_DIR_EXCLUDED EQUAL -1)
        return() # 如果目录被排除，直接返回，不再处理该目录及其子目录
    endif ()

    # 2. 收集当前目录下的所有潜在源文件
    file(GLOB COLLECTED_SOURCES
            ${current_dir}/*.c
            ${current_dir}/*.cc
            ${current_dir}/*.cpp
            ${current_dir}/*.cxx
            ${current_dir}/*.h
            ${current_dir}/*.hh
            ${current_dir}/*.hpp)

    # 3. 逐个检查文件是否在排除列表中 (支持文件排除)
    foreach (FILE_PATH ${COLLECTED_SOURCES})
        list(FIND ARGN "${FILE_PATH}" IS_FILE_EXCLUDED)
        if (IS_FILE_EXCLUDED EQUAL -1)
            list(APPEND ${variable} ${FILE_PATH}) # 只有不在列表中的文件才添加
        endif ()
    endforeach ()

    # 4. 递归处理子目录
    file(GLOB SUB_DIRECTORIES ${current_dir}/*)
    foreach(SUB_DIRECTORY ${SUB_DIRECTORIES})
        if (IS_DIRECTORY ${SUB_DIRECTORY})
            # 将 ARGN (排除列表) 继续传递给下一层递归
            PickSourceFiles("${SUB_DIRECTORY}" "${variable}" "${ARGN}")
        endif ()
    endforeach()

    # 5. 将结果更新到父作用域 [cite: 2]
    set(${variable} ${${variable}} PARENT_SCOPE)
endfunction()

function(PickIncludeDirectories current_dir variable)
    # 1. 检查当前目录是否在排除列表中
    list(FIND ARGN "${current_dir}" IS_EXCLUDED)
    if (NOT IS_EXCLUDED EQUAL -1)
        return() # 如果目录在排除名单中，直接跳过，不添加也不递归其子目录
    endif ()

    # 2. 将当前目录添加到包含路径列表
    list(APPEND ${variable} ${current_dir})

    # 3. 递归处理子目录
    file(GLOB SUB_DIRECTORIES ${current_dir}/*)
    foreach(SUB_DIRECTORY ${SUB_DIRECTORIES})
        if (IS_DIRECTORY ${SUB_DIRECTORY})
            PickIncludeDirectories("${SUB_DIRECTORY}" "${variable}" "${ARGN}")
        endif ()
    endforeach()

    # 4. 更新父作用域变量
    set(${variable} ${${variable}} PARENT_SCOPE)
endfunction()

macro(MakeFilter dir)
file(GLOB_RECURSE elements RELATIVE ${dir} *.h *.hh *.hpp *.c *.cc *.cxx *.cpp)
foreach(element ${elements})
  get_filename_component(element_name ${element} NAME)
  get_filename_component(element_dir ${element} DIRECTORY)
  if (NOT ${element_dir} STREQUAL "")
      string(REPLACE "/" "\\" group_name ${element_dir})
      source_group("${group_name}" FILES ${dir}/${element})
#      message("${group_name}" FILES ${dir}/${element})
  else()
    source_group("\\" FILES ${dir}/${element})
#    message("\\" FILES ${dir}/${element})
  endif()
endforeach()
endmacro()

macro(configure_files srcDir destDir)
#    message(STATUS "Configuring directory ${destDir}")
    file(MAKE_DIRECTORY ${destDir})
    file(GLOB templateFiles RELATIVE ${srcDir} ${srcDir}/*)
    foreach(templateFile ${templateFiles})
        set(srcTemplatePath ${srcDir}/${templateFile})
        if(NOT IS_DIRECTORY ${srcTemplatePath})
#            message(STATUS "Configuring file ${templateFile}")
            configure_file(
                    ${srcTemplatePath}
                    ${destDir}/${templateFile}
                    @ONLY)
        endif(NOT IS_DIRECTORY ${srcTemplatePath})
    endforeach(templateFile)
endmacro(configure_files)