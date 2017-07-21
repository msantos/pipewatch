#!/usr/bin/env bats

@test "exit: test pipeline success" {
    pipewatch "echo xxx" "cat" "grep --line-buffered x" "exec cat"
}

@test "exit: test pipeline success with monitor enabled" {
    pipewatch --monitor "echo xxx" "cat" "grep --line-buffered x" "exec cat"
}

@test "exit: test pipeline error (first command)" {
    run pipewatch --verbose "./doesnotexist" "grep --line-buffered a" "exec cat"
    cat<<EOF
$output
EOF
    [ "$status" -eq "127" ]
}

@test "exit: test pipeline error (middle command)" {
    run pipewatch --verbose "echo xxx" "cat" "grep --line-buffered a" "exec cat"
    cat<<EOF
$output
EOF
    [ "$status" -eq "1" ]
}

@test "exit: test pipeline error (last command)" {
    run pipewatch --verbose "sleep 300" "./doesnotexist"
    cat<<EOF
$output
EOF
    [ "$status" -eq "127" ]
}
