if(NOT DEFINED HTML_FILE OR HTML_FILE STREQUAL "")
    message(FATAL_ERROR "HTML_FILE must name the generated documentation page")
endif()

if(NOT EXISTS "${HTML_FILE}")
    message(FATAL_ERROR "Generated documentation page not found: ${HTML_FILE}")
endif()

file(READ "${HTML_FILE}" html)

string(
    REPLACE
    "docs/assets/neuralplus-social-preview.png"
    "neuralplus-social-preview.png"
    html
    "${html}"
)

set(marker "<!-- NeuralPlus social preview metadata -->")
string(FIND "${html}" "${marker}" marker_position)
if(NOT marker_position EQUAL -1)
    return()
endif()

set(metadata [=[
<!-- NeuralPlus social preview metadata -->
<link rel="canonical" href="https://neuralplus.dev/"/>
<meta name="description" content="The composable AI systems toolkit for C++."/>
<meta property="og:type" content="website"/>
<meta property="og:site_name" content="NeuralPlus"/>
<meta property="og:title" content="NeuralPlus — The composable AI systems toolkit for C++."/>
<meta property="og:description" content="A provider-independent C++17 SDK for AI conversations, tool use, session state, and tracing."/>
<meta property="og:url" content="https://neuralplus.dev/"/>
<meta property="og:image" content="https://neuralplus.dev/neuralplus-social-preview.png"/>
<meta property="og:image:width" content="1200"/>
<meta property="og:image:height" content="630"/>
<meta property="og:image:alt" content="NeuralPlus — The composable AI systems toolkit for C++."/>
<meta name="twitter:card" content="summary_large_image"/>
<meta name="twitter:title" content="NeuralPlus — The composable AI systems toolkit for C++."/>
<meta name="twitter:description" content="A provider-independent C++17 SDK for AI conversations, tool use, session state, and tracing."/>
<meta name="twitter:image" content="https://neuralplus.dev/neuralplus-social-preview.png"/>
]=])

string(FIND "${html}" "</head>" head_end)
if(head_end EQUAL -1)
    message(FATAL_ERROR "Generated documentation page has no closing </head>")
endif()

string(REPLACE "</head>" "${metadata}</head>" html "${html}")
file(WRITE "${HTML_FILE}" "${html}")
