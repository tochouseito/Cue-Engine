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
    cueRepositoryCppFiles
    LIST_DIRECTORIES FALSE
    "${REPOSITORY_ROOT}/*.cpp"
    "${REPOSITORY_ROOT}/*.h"
    "${REPOSITORY_ROOT}/*.hpp"
    "${REPOSITORY_ROOT}/*.ixx"
)

list(
    FILTER
    cueRepositoryCppFiles
    EXCLUDE
    REGEX
    "[/\\\\](\\.git|\\.vs|out|build)[/\\\\]"
)

foreach(cueRepositoryCppFile IN LISTS cueRepositoryCppFiles)
    file(READ "${cueRepositoryCppFile}" cueRepositoryCppContent)
    string(TOLOWER "${cueRepositoryCppContent}" cueRepositoryCppContentLower)

    if(
        cueRepositoryCppContentLower
        MATCHES
        "directxmath(\\.h)?|directx::[ \\t\\r\\n]*xm|xmvector|xmmatrix|xmfloat[0-9a-z_]*|xmuint[0-9a-z_]*|xmint[0-9a-z_]*"
    )
        message(
            FATAL_ERROR
            "Repository-owned C++ source has a forbidden DirectXMath dependency: ${cueRepositoryCppFile}"
        )
    endif()
endforeach()
