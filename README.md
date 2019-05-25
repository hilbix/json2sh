> **Warning!** Versions before 2.0 had a critical bug!
>
> This is nearly not tested!

[![json2sh dev Build Status](https://api.cirrus-ci.com/github/hilbix/json2sh.svg?branch=master)](https://cirrus-ci.com/github/hilbix/json2sh/master)


# `json2sh` converts JSON into shell compatible output

## Usage

	git clone https://github.com/hilbix/json2sh.git
	cd json2sh
	make
	sudo make install

then

	json2sh <<<'{"w":"t", "f":[ 6 ]}'

or

	json2sh 'x _' ' ' <<<'{"w":"t", "f":[ 6 ]}'

you can even source it:

	. <(json2sh <<<'{"w":"t", "f":[ 6 ]}')
	echo $JSON__0_w

This converter is incremental:
- It only keeps the last value in memory.  So the document can be much bigger than the available RAM.
- And it outputs things immediately when they are received.  Only 256 bytes of a value is buffered before it is output.

The output is bash compatible.  It uses following constructs:

- `value` for simple values
- `'value'` for normal values (which do not include a `'`)
- `$'value'` for complex values (those which contain `'` or control characters)

For the variable names:

- `A`-`Z`, `a`-`z`, `0`-`9` are used as is.
- `_` becomes `__` (doubled), also in escape mode, `c` is `_`
- Everything else is escaped as `_ESCAPE_` with ESCAPE made of:
  - `abdefnrtv` are the corresponding control character (where `\d` is `DEL` `7F` 127)
  - `ghijklmopqsuwxyz` are the HEX nibbles in reverse (`z`=0, `g`=15, always in pairs)
  - `0` stands for a `.` separating the object identifier parts (left away after indexes)
  - `1`-`9` followed by other digits including `0` are array indexes.
  - `A`-`Z` switches to a different Unicode planes, `A`=0 `Z`=25, `BA`=26 and so on,
     followed by two HEX nibbles (except for plane 0, which can have controls).


## FAQ

WTF?

- Yes, it works, indeed.

Really?

- Sure!

What is it able to process?

- http://www.json.org/
- `true` becomes `$JSON_true_`
- `false` becomes `$JSON_false_`
- `null` becomes `$JSON_null_`
- Empty array becomes `$JSON_empty_`
- Empty object becomes `$JSON_nothing_`

Return codes?

- 0 if JSON parses OK
- 23 if something fails (OOPS)
- 42 for usage

Contact/Bugs?

- Open issue at GitHub

Contribution/Improvements?

- Open Pull Request at GitHub

License?

- This Works is placed under the terms of the Copyright Less License,
  see file COPYRIGHT.CLL.  USE AT OWN RISK, ABSOLUTELY NO WARRANTY.

- Read: This is free as free beer, free speech and free baby.

