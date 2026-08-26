# /// @brief C++ Source から Comment を除去し、Literal 内容を隠した Code View も生成する
function(cue_strip_cpp_comments inputContents outputVariable codeOutputVariable)
    set(strippedContents "")
    set(codeContents "")
    set(lexicalState Normal)
    set(characterIndex 0)
    string(LENGTH "${inputContents}" inputLength)

    while(characterIndex LESS inputLength)
        string(SUBSTRING "${inputContents}" ${characterIndex} 1 character)
        math(EXPR nextCharacterIndex "${characterIndex} + 1")

        if(nextCharacterIndex LESS inputLength)
            string(SUBSTRING "${inputContents}" ${nextCharacterIndex} 1 nextCharacter)
        else()
            set(nextCharacter "")
        endif()

        if(lexicalState STREQUAL Normal)
            if(character STREQUAL "/" AND nextCharacter STREQUAL "/")
                string(APPEND strippedContents "  ")
                string(APPEND codeContents "  ")
                set(lexicalState LineComment)
                math(EXPR characterIndex "${characterIndex} + 2")
                continue()
            elseif(character STREQUAL "/" AND nextCharacter STREQUAL "*")
                string(APPEND strippedContents "  ")
                string(APPEND codeContents "  ")
                set(lexicalState BlockComment)
                math(EXPR characterIndex "${characterIndex} + 2")
                continue()
            elseif(character STREQUAL "R" AND nextCharacter STREQUAL "\"")
                math(EXPR rawDelimiterStart "${characterIndex} + 2")
                math(EXPR rawRemainderLength "${inputLength} - ${rawDelimiterStart}")
                string(
                    SUBSTRING
                    "${inputContents}"
                    ${rawDelimiterStart}
                    ${rawRemainderLength}
                    rawRemainder
                )
                string(FIND "${rawRemainder}" "(" rawOpenOffset)

                if(NOT rawOpenOffset EQUAL -1 AND rawOpenOffset LESS_EQUAL 16)
                    string(SUBSTRING "${rawRemainder}" 0 ${rawOpenOffset} rawDelimiter)
                    set(rawClosingSequence ")${rawDelimiter}\"")
                    math(EXPR rawBodyOffset "${rawOpenOffset} + 1")
                    math(EXPR rawBodyLength "${rawRemainderLength} - ${rawBodyOffset}")
                    string(
                        SUBSTRING
                        "${rawRemainder}"
                        ${rawBodyOffset}
                        ${rawBodyLength}
                        rawBody
                    )
                    string(FIND "${rawBody}" "${rawClosingSequence}" rawCloseOffset)

                    if(NOT rawCloseOffset EQUAL -1)
                        string(LENGTH "${rawClosingSequence}" rawClosingLength)
                        math(
                            EXPR
                            rawLiteralLength
                            "2 + ${rawBodyOffset} + ${rawCloseOffset} + ${rawClosingLength}"
                        )
                        string(
                            SUBSTRING
                            "${inputContents}"
                            ${characterIndex}
                            ${rawLiteralLength}
                            rawLiteral
                        )
                        string(REGEX REPLACE "[^\r\n]" " " rawLiteral "${rawLiteral}")
                        string(APPEND strippedContents "${rawLiteral}")
                        string(APPEND codeContents "${rawLiteral}")
                        math(EXPR characterIndex "${characterIndex} + ${rawLiteralLength}")
                        continue()
                    endif()
                endif()
            elseif(character STREQUAL "%" AND nextCharacter STREQUAL ":")
                string(APPEND strippedContents "#")
                string(APPEND codeContents "#")
                math(EXPR characterIndex "${characterIndex} + 2")
                continue()
            elseif(character STREQUAL "\"")
                string(APPEND strippedContents "${character}")
                string(APPEND codeContents " ")
                set(lexicalState StringLiteral)
                math(EXPR characterIndex "${characterIndex} + 1")
                continue()
            elseif(character STREQUAL "'")
                string(APPEND strippedContents "${character}")
                string(APPEND codeContents " ")
                set(lexicalState CharacterLiteral)
                math(EXPR characterIndex "${characterIndex} + 1")
                continue()
            endif()

            string(APPEND strippedContents "${character}")
            string(APPEND codeContents "${character}")
        elseif(lexicalState STREQUAL StringLiteral OR lexicalState STREQUAL CharacterLiteral)
            string(APPEND strippedContents "${character}")

            if(character STREQUAL "\n" OR character STREQUAL "\r")
                string(APPEND codeContents "${character}")
            else()
                string(APPEND codeContents " ")
            endif()

            if(character STREQUAL "\\" AND nextCharacterIndex LESS inputLength)
                string(APPEND strippedContents "${nextCharacter}")

                if(nextCharacter STREQUAL "\n" OR nextCharacter STREQUAL "\r")
                    string(APPEND codeContents "${nextCharacter}")
                else()
                    string(APPEND codeContents " ")
                endif()

                math(EXPR characterIndex "${characterIndex} + 2")
                continue()
            elseif(lexicalState STREQUAL StringLiteral AND character STREQUAL "\"")
                set(lexicalState Normal)
            elseif(lexicalState STREQUAL CharacterLiteral AND character STREQUAL "'")
                set(lexicalState Normal)
            endif()
        elseif(lexicalState STREQUAL LineComment)
            if(character STREQUAL "\n")
                string(APPEND strippedContents "\n")
                string(APPEND codeContents "\n")
                set(lexicalState Normal)
            elseif(character STREQUAL "\r")
                string(APPEND strippedContents "\r")
                string(APPEND codeContents "\r")
            else()
                string(APPEND strippedContents " ")
                string(APPEND codeContents " ")
            endif()
        elseif(lexicalState STREQUAL BlockComment)
            if(character STREQUAL "*" AND nextCharacter STREQUAL "/")
                string(APPEND strippedContents "  ")
                string(APPEND codeContents "  ")
                set(lexicalState Normal)
                math(EXPR characterIndex "${characterIndex} + 2")
                continue()
            elseif(character STREQUAL "\n" OR character STREQUAL "\r")
                string(APPEND strippedContents "${character}")
                string(APPEND codeContents "${character}")
            else()
                string(APPEND strippedContents " ")
                string(APPEND codeContents " ")
            endif()
        endif()

        math(EXPR characterIndex "${characterIndex} + 1")
    endwhile()

    set(${outputVariable} "${strippedContents}" PARENT_SCOPE)
    set(${codeOutputVariable} "${codeContents}" PARENT_SCOPE)
endfunction()

# /// @brief Report 本文に必須 Text が含まれることを検証する
function(cue_require_report_substring reportContents requiredText errorPrefix)
    string(FIND "${reportContents}" "${requiredText}" textPosition)

    if(textPosition EQUAL -1)
        message(FATAL_ERROR "${errorPrefix}${requiredText}")
    endif()
endfunction()

# /// @brief Report の Line Collection に必須の完全一致 Line が含まれることを検証する
function(cue_require_report_line reportLinesVariable requiredLine errorPrefix)
    set(hasRequiredLine FALSE)

    foreach(reportLine IN LISTS ${reportLinesVariable})
        if(reportLine STREQUAL requiredLine)
            set(hasRequiredLine TRUE)
            break()
        endif()
    endforeach()

    if(NOT hasRequiredLine)
        message(FATAL_ERROR "${errorPrefix}${requiredLine}")
    endif()
endfunction()
