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

file(
    GLOB_RECURSE
    cueEngineCppFiles
    LIST_DIRECTORIES FALSE
    "${ENGINE_SOURCE_DIR}/*.cpp"
    "${ENGINE_SOURCE_DIR}/*.h"
    "${ENGINE_SOURCE_DIR}/*.hpp"
    "${ENGINE_SOURCE_DIR}/*.ixx"
)

foreach(cueEngineCppFile IN LISTS cueEngineCppFiles)
    file(READ "${cueEngineCppFile}" cueEngineCppContent)
    string(TOLOWER "${cueEngineCppContent}" cueEngineCppContentLower)

    if(cueEngineCppContentLower MATCHES "directxmath\\.h|xmvector|xmmatrix")
        message(
            FATAL_ERROR
            "Engine-owned C++ source has a forbidden DirectXMath dependency: ${cueEngineCppFile}"
        )
    endif()
endforeach()
