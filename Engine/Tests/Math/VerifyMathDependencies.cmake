file(
    GLOB_RECURSE
    cueMathSourceFiles
    LIST_DIRECTORIES FALSE
    "${MATH_SOURCE_DIR}/*.cpp"
    "${MATH_SOURCE_DIR}/*.h"
)

foreach(cueMathSourceFile IN LISTS cueMathSourceFiles)
    file(READ "${cueMathSourceFile}" cueMathSourceContent)
    string(TOLOWER "${cueMathSourceContent}" cueMathSourceContentLower)

    if(cueMathSourceContentLower MATCHES "cue/(platform|rhi|runtimehost|editor)/")
        message(FATAL_ERROR "Cue.Math has a forbidden Cue module include: ${cueMathSourceFile}")
    endif()

    if(cueMathSourceContentLower MATCHES "directxmath|windows\.h|d3d12\.h|intrin\.h")
        message(FATAL_ERROR "Cue.Math has a forbidden platform or SIMD include: ${cueMathSourceFile}")
    endif()
endforeach()
