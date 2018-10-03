# CONTRIBUTING

For the sake of saving both your time and mine, please read the following guidelines before making a contribution to this project.

## OPENING ISSUES

When opening an issue, please first check for an existing issue in the project (in **both** the closed and open issues) that may cover the issue you're experiencing. If an issue is closed and you think it should not be, please feel free to add a new comment in a closed issue thread; I will be notified, and I'll reconsider whether the issue should be re-opened based on your comment.

If you determine that you need to open a new issue, please include the following details:

  * Your operating system (e.g., Ubuntu 18.04).
  * Your system's CPU architecture (e.g., x86-64).
  * Your C compiler's version, as shown by, e.g., `clang -v`.
  * What steps are necessary to reproduce the issue.

## PULL REQUESTS

If you would like to make a pull request (PR) or other contribution to this project, please first refer to the following guidelines.

### One PR per feature

Please make one PR/patch per feature. Do not lump multiple features into a single PR. One PR per feature is a hassle for contributors, and for that I am sorry; but it makes reviewing contributions easier, and it also means that, if your contribution creates a bug or other issue, it's easier to track down the root cause. This policy also increases the likelihood that your PRs will be accepted in the case where there is an issue with one part of your PR, but the rest is fine.

If you are in doubt about what constitutes "a feature," please contact me before making a pull request so that we can sort it out. Feel free to do this by opening an issue that describes what you're proposing to do.

### Testing

Make sure you have run all tests successfully before submitting your PR. If you are adding functionality, please add new tests to the test suite to exercise that functionality. If your PR fixes a bug, please write a test(s) that demonstrates:

  1. how to trigger the bug on the existing code base, and 
  2. that once your code has been applied, the bug is fixed.

### Bug fixes

If your PR is a bug fix, please first submit an issue that describes the bug in detail and how it occurs (see [OPENING ISSUES](#opening-issues)), and refer to this issue when submitting the PR.

### Public domain

By submitting a PR to this project, you are agreeing that your contribution is dedicated to the public domain per the
[COPYING](../COPYING) file included in this distribution, and that you are waiving any copyright claims with respect to your contribution. You also assert that this contribution is your own creation, and not taken from another work.

### Commit messages

A commit message should consist of at least a single line describing the changes made by the commit. This line should not be too many characters; I'm not a stickler about line length, but please try to stay below 80 characters or so.

If one line cannot capture what you want to say about the commit, please feel free to add more detail in subsequent paragraphs. In general, I prefer commit messages that are more detailed than not. Commit messages with details make it possible to understand the motivation for the change you've made. 

([Here](https://github.com/dhess/c-ringbuf/commit/21669475d7f4e13801f94f5031dbd9aa00e95796) is an example of one of my own commits where the commit message adds some important detail motivating why the change was made, and what impacts it might have on the code.)

There's no need to go overboard, however. If your commit is simple and straightforward, a simple and straightforward single line description will suffice.

If your commit is a bug fix, please add the following text somewhere in your commit message, obviously replacing "#13" with the issue number that your commit addresses:

`Fixes #13`

I reserve the right to reject PRs purely based on whether the commit message is adequate/accurate.

### Code formatting

Please respect the code formatting rules I've used for this project. There are a few hard and fast rules:

  * Spaces, not tabs.
  * Indent by 4 spaces.
  * Macros in `UPPER_CASE`, using underscores (`_`) as separators.
  * Everything else in `lower_case`, using underscores (`_`) as separators.

For other formatting, I'm less picky, but generally speaking, just look for an example of in the existing code base and follow it. (It's quite possible that I myself have been inconsistent in a few places. Feel free to point these cases out to me by opening an issue, if you like.)

I reserve the right to reject PRs purely based on their formatting. Please do not take it personally it if happens to you; everyone has their own quirky way of formatting code, and mine is no better than anyone else's, but I think it's important to be consistent within a project.
