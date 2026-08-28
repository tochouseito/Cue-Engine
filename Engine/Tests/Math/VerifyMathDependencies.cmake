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
    "${REPOSITORY_ROOT}/*.c++"
    "${REPOSITORY_ROOT}/*.cc"
    "${REPOSITORY_ROOT}/*.cpp"
    "${REPOSITORY_ROOT}/*.cppm"
    "${REPOSITORY_ROOT}/*.cxx"
    "${REPOSITORY_ROOT}/*.cxxm"
    "${REPOSITORY_ROOT}/*.h"
    "${REPOSITORY_ROOT}/*.h++"
    "${REPOSITORY_ROOT}/*.hh"
    "${REPOSITORY_ROOT}/*.hpp"
    "${REPOSITORY_ROOT}/*.hxx"
    "${REPOSITORY_ROOT}/*.inl"
    "${REPOSITORY_ROOT}/*.ipp"
    "${REPOSITORY_ROOT}/*.ixx"
    "${REPOSITORY_ROOT}/*.tpp"
)

set(
    cueDirectXMathIdentifierPattern
    "(^|[^A-Za-z0-9_])([CFGH]XMVECTOR|[CF]XMMATRIX|XM_[A-Z0-9_]+|XM256_STREAM_PS|XM(ASSERT|FINLINE|GLOBALCONST(EX)?|INLINE|CONSTEXPR)|XMMax|XMMin|g_XM[A-Za-z0-9_]*|XMVECTOR[A-Z0-9_]*|XMMATRIX|XM(FLOAT|INT|UINT|BYTE|UBYTE|SHORT|USHORT|HALF|COLOR|HENDN|DEC|UDEC|XDEC|UXDEC|UNIBBLE|U555|U565)[A-Z0-9_]*|XM(Vector|Matrix|Quaternion|Plane|Color|Scalar|Convert|Load|Store|Verify|Comparison|SinCos|Fresnel)[A-Za-z0-9_]*)($|[^A-Za-z0-9_])"
)

foreach(cueRepositoryCppFile IN LISTS cueRepositoryCppFiles)
    file(
        RELATIVE_PATH
        cueRepositoryCppRelativePath
        "${REPOSITORY_ROOT}"
        "${cueRepositoryCppFile}"
    )

    if(cueRepositoryCppRelativePath MATCHES "^(\\.git|\\.vs|out|build)[/\\\\]")
        continue()
    endif()

    file(READ "${cueRepositoryCppFile}" cueRepositoryCppContent)
    string(TOLOWER "${cueRepositoryCppContent}" cueRepositoryCppContentLower)

    if(cueRepositoryCppContentLower MATCHES
       "directx(math|packedvector|collision|colors)(\\.h)?|using[ \\t\\r\\n]+namespace[ \\t\\r\\n]+directx($|[^a-z0-9_])|namespace[ \\t\\r\\n]+[a-z_][a-z0-9_]*[ \\t\\r\\n]*=[ \\t\\r\\n]*(::[ \\t\\r\\n]*)?directx($|[^a-z0-9_])|directx::[ \\t\\r\\n]*(xm|packedvector|colors(linear)?|bounding(sphere|box|orientedbox|frustum)|containmenttype|planeintersectiontype|triangletests)"
       OR cueRepositoryCppContent MATCHES "${cueDirectXMathIdentifierPattern}")
        message(
            FATAL_ERROR
            "Repository-owned C++ source has a forbidden DirectXMath dependency: ${cueRepositoryCppFile}"
        )
    endif()
endforeach()
