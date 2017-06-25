#!/usr/bin/env bats

@test "exit: test pipeline success" {
    pipewatch "echo xxx" "cat" "grep --line-buffered x" "exec cat"
}

@test "exit: test pipeline success with monitor enabled" {
    pipewatch -m "echo xxx" "cat" "grep --line-buffered x" "exec cat"
}

@test "exit: test pipeline error (first command)" {
    run pipewatch -v "./doesnotexist" "grep --line-buffered a" "exec cat"
    cat<<EOF
$output
EOF
    [ "$status" -eq "127" ]
}

@test "exit: test pipeline error (middle command)" {
    run pipewatch -v "echo xxx" "cat" "grep --line-buffered a" "exec cat"
    cat<<EOF
$output
EOF
    [ "$status" -eq "1" ]
}

@test "exit: test pipeline error (last command)" {
    run pipewatch -v "sleep 300" "./doesnotexist"
    cat<<EOF
$output
EOF
    [ "$status" -eq "127" ]
}
