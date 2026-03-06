function(apply_velox_compiler_flags target_name)
  if(MSVC)
    target_compile_options(
      ${target_name}
      PRIVATE
        /W4
        /permissive-
        /EHsc
        /Zc:__cplusplus
        /utf-8
        /MP
        $<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:/O2>
        $<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:/GL>
        $<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:/Gy>
        $<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:/Gw>)

    target_link_options(${target_name} PRIVATE $<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:/LTCG>)
  else()
    target_compile_options(${target_name} PRIVATE -Wall -Wextra -Wpedantic)
  endif()
endfunction()
