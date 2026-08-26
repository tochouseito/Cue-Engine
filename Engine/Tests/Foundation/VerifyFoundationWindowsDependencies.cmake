cmake_minimum_required(VERSION 4.2.0)

include("${CMAKE_CURRENT_LIST_DIR}/../CMake/CueDependencyVerification.cmake")

if(NOT DEFINED REPORT_FILE OR NOT EXISTS "${REPORT_FILE}")
    message(FATAL_ERROR "Foundation Windows dependency report was not generated")
endif()

file(STRINGS "${REPORT_FILE}" dependencyReportLines)

foreach(
    requiredLine
    IN ITEMS
        "Cue.Foundation.Windows LINK_LIBRARIES: Cue.Foundation"
        "Cue.Foundation.Windows INTERFACE_LINK_LIBRARIES: Cue.Foundation"
        "Allowed graph: Cue.Foundation.Windows->Cue.Foundation"
        "Cue.Foundation core Windows dependency: forbidden"
        "Upper module dependencies: forbidden"
)
    cue_require_report_line(
        dependencyReportLines
        "${requiredLine}"
        "Foundation Windows dependency report is missing an exact line: "
    )
endforeach()
