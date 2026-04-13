#
# The MIT License (MIT)
#
# Copyright (c) 2020 Tobias Koch <tobias.koch@gmail.com>
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.
#

# Return the argument placeholder for an option of the given subcommand,
# or nothing if the option takes no argument.
_build_box_opt_takes_arg() {
    case "$1" in
        create)
            case "$2" in
                -r|--release)  echo '<name>'   ;;
                -a|--arch)     echo '<arch>'   ;;
                -l|--libc)     echo '<libc>'   ;;
                --repo-base)   echo '<url>'    ;;
                -t|--targets)  echo '<dir>'    ;;
            esac
            ;;
        info)
            case "$2" in
                -k|--key)      echo '<key>'    ;;
                -t|--targets)  echo '<dir>'    ;;
            esac
            ;;
        delete|list)
            case "$2" in
                -t|--targets)  echo '<dir>'    ;;
            esac
            ;;
        login|run|mount)
            case "$2" in
                -m|--mount)    echo '<fstype>' ;;
                -t|--targets)  echo '<dir>'    ;;
            esac
            ;;
        umount)
            case "$2" in
                -m|--umount)   echo '<fstype>' ;;
                -t|--targets)  echo '<dir>'    ;;
            esac
            ;;
    esac
}

_build_box_arg_complete() {
    local _subcommand="${COMP_WORDS[1]}"
    local _previous_arg="${COMP_WORDS[$(($COMP_CWORD-1))]}"

    local _opt_arg=""

    case "$_previous_arg" in
        -*)
            _opt_arg=$(_build_box_opt_takes_arg "$_subcommand" "$_previous_arg")
            ;;
    esac

    case "$_opt_arg" in
        '<arch>')
            COMPREPLY=(
                $(
                    compgen -W "aarch64 loongarch64 mips64el powerpc64le riscv64 s390x x86_64" \
                        -- ${COMP_WORDS[COMP_CWORD]}
                )
            )
            return
            ;;
        '<dir>')
            _cd
            return
            ;;
        '<fstype>')
            COMPREPLY=(
                $(
                    compgen -W "dev proc sys home" -- ${COMP_WORDS[COMP_CWORD]}
                )
            )
            return
            ;;
        '<libc>')
            COMPREPLY=(
                $(
                    compgen -W "musl glibc" -- ${COMP_WORDS[COMP_CWORD]}
                )
            )
            return
            ;;
    esac

    local _word_counter=0
    local _varg_counter=0
    local _target_dir="/var/lib/build-box/users/$(id -u)/targets"

    while [ "$_word_counter" -lt "${#COMP_WORDS[*]}" ]; do
        local _word="${COMP_WORDS[$_word_counter]}"

        case "$_word" in
            -t|--targets)
                case "${COMP_WORDS[$(($_word_counter+1))]}" in
                    =)
                        _target_dir="${COMP_WORDS[$(($_word_counter+2))]}"
                        _word_counter=$(($_word_counter+3))
                        ;;
                    *)
                        _target_dir="${COMP_WORDS[$(($_word_counter+1))]}"
                        _word_counter=$(($_word_counter+2))
                        ;;
                esac
                ;;
            -*)
                _opt_arg=$(_build_box_opt_takes_arg "$_subcommand" "$_word")

                if [ -n "$_opt_arg" ]; then
                    case "${COMP_WORDS[$(($_word_counter+1))]}" in
                        =)
                            _word_counter=$(($_word_counter+3))
                            ;;
                        *)
                            _word_counter=$(($_word_counter+2))
                            ;;
                    esac
                else
                    _word_counter=$(($_word_counter+1))
                fi
                ;;
            *)
                _word_counter=$(($_word_counter+1))
                _varg_counter=$(($_varg_counter+1))
                ;;
        esac
    done

    # Determine expected positional argument based on subcommand and position.
    # _varg_counter includes "build-box" (1) and the subcommand (2), so the
    # first positional argument corresponds to _varg_counter == 3.
    local _expected_arg=""

    case "$_subcommand" in
        create)
            [ "$_varg_counter" -ge 4 ] && _expected_arg='<spec>'
            ;;
        delete)
            [ "$_varg_counter" -ge 3 ] && _expected_arg='<target-name>'
            ;;
        info|login|mount|umount|run)
            [ "$_varg_counter" -eq 3 ] && _expected_arg='<target-name>'
            ;;
    esac

    case "$_expected_arg" in
        '<target-name>')
            COMPREPLY=(
                $(
                    compgen -W "$(
                        ${COMP_WORDS[0]} list --targets="$_target_dir" | \
                            cut -d' ' -f1
                    )" -- ${COMP_WORDS[COMP_CWORD]}
                )
            )
            ;;
        '<dir>')
            _cd
            ;;
        '<spec>')
            compopt -o default
            COMPREPLY=()
            ;;
    esac
}

_build_box_opt_complete() {
    local _opts="-h --help"

    case "${COMP_WORDS[1]}" in
        create)
            _opts="$_opts -r --release -a --arch -l --libc --force --repo-base --no-verify"
            ;;
        delete|list)
            _opts="$_opts"
            ;;
        info)
            _opts="$_opts --json -k --key"
            ;;
        login)
            _opts="$_opts -m --mount --no-mount --no-file-copy"
            ;;
        run)
            _opts="$_opts --isolate -m --mount --no-mount --no-file-copy"
            ;;
        mount)
            _opts="$_opts -m --mount"
            ;;
        umount)
            _opts="$_opts -m --umount"
            ;;
    esac

    COMPREPLY=($(compgen -W "$_opts" -- ${COMP_WORDS[COMP_CWORD]}))
}

_build_box_complete() {
    local _valid_commands="create delete info list login mount run umount"

    case "$COMP_CWORD" in
        1)
            COMPREPLY=(
                $(
                    compgen -W "$_valid_commands" \
                        -- ${COMP_WORDS[COMP_CWORD]}
                )
            )
            ;;
        *)
            case "${COMP_WORDS[1]}" in
                create|delete|info|list|login|mount|run|umount)
                    case "${COMP_WORDS[COMP_CWORD]}" in
                        -*)
                            _build_box_opt_complete
                            ;;
                        *)
                            _build_box_arg_complete
                            ;;
                    esac
                    ;;
            esac
            ;;
    esac
}

complete -F _build_box_complete build-box
