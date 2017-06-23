#!/usr/bin/env bats

@test "exit: test pipeline success" {
    pipewatch "echo xxx" "cat" "grep --line-buffered x" "exec cat"
}

@test "exit: test pipeline success with monitor enabled" {
    pipewatch -m "echo xxx" "cat" "grep --line-buffered x" "exec cat"
}

@test "exit: test pipeline error" {
    run pipewatch -v "echo xxx" "cat" "grep --line-buffered a" "exec cat"
    cat<<EOF
$output
EOF
    [ "$status" -eq "1" ]
}
