# NEXTPUZZLE

`nextpuzzle` is a commandline utility for spaced repetition learning on chess.com puzzles.  It uses an sqlite3 database to keep track of how many times the user has succeeded or failed at a given puzzle and to calculate and store, based on this rate, the next day a puzzle should be worked.  Repetition spaces are calculated by feeding the cumulative number of successes in a row into a fibonacci sequence calculator and going out that number of days from today.  Failure resets the puzzle to day 0.

## Example


A puzzle worked successfully on 2022-12-31 will have score 1 and will need to be worked again on 2023-01-01, since fibonacci(1) is 1 and 1 day from 2022-12-31 is 2023-01-01.

The same puzzle worked successfully on 2023-01-01 will have score 2 and will need to be worked again on 2023-01-03, since fibonacci(2) is 2 and 2 days from 2023-01-01 is 2023-01-03.

The same puzzle worked successfully on 2023-01-03 will have score 3 and will need to be worked again on 2023-01-06, since fibonacci(3) is 3 and 3 days from 2023-01-03 is 2023-01-06.

The same puzzle worked successfully on 2023-01-06 will have score 4 and will need to be worked again on 2023-01-11, since fibonacci(4) is 5 and 5 days from 2023-01-06 is 2023-01-11.

The same puzzle failed on 2023-01-11 will have score 0 and will need to be worked again on 2023-01-12, since fibonacci(0) is 1 and 1 days from 2023-01-11 is 2023-01-12.

## CLI

`nextpuzzle` is a cli that accepts the following commands, some of which require a parameter:

1. `next` - gets the next puzzle for the current day; prints a success message if there are no more puzzles for today.
1. `<puzzle_id> s|f` - records success or failure for a given puzzle id.  If this is a new puzzle with 's' or an existing puzzle id with 'f', it sets the puzzle score to `0` and queues it for work the next day. If this is an existing puzzle id with 's' it increments the score for that puzzle by 1 and calculates the next day it should be worked according to the algoritm above.
1. `stats` - prints an overall success and failure rate
