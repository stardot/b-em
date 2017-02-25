Developing for b-em
===================

This document aims to help the developer with working on the b-em codebase.

Branch Workflows / Submitting Code Changes
==========================================

The main b-em repository treats the `master` branch as stable, in that it's the
branch which has the most tested code on it, and the branch from which releases
are made.  Formal releases of b-em are tagged, in the form `x.y`.  Untagged
code may well accumulate on `master`, which will go to form the next release.

Other branches in the repository will reflect on-going development from core
developers.   As such, these branches are often in a state of flux, and likely
to be rebased against other branches.  *NO* code should be based off topic
branches, unless explicitly agreed with other developers, who might need to
collaborate.

### Branch naming

Branch names are used to try and indicate the feature, and who is working on
them.  So for example, a topic-branch will be named as:

`initials/rough-description-of-branch`

For example:

`ta/fix-clang-warnings`

denotes that the branch is worked on by someone with the initials `TA` and that
the branch is about fixing warnings from Clang.

Sometimes, if more than one person is collaborating on a branch, the initials
prefix might not be needed.

### Submitting Pull-requests

External contributions are always welcomed and encouraged.  If you're thinking
of writing a new feature, it is worthwhile opening an issue against b-em
to discuss whether it's a good idea, and to check no one else is working on
that feature.  Additionally, you can also post to
[the main b-em thread on the stardot forums](http://stardot.org.uk/forums/viewtopic.php?f=4&t=10823)

Those wishing to submit code/bug-fixes should:

* [Fork the b-em-repository](https://github.com/stardot/b-em#fork-destination-box)
* If not already done so, clone that repository locally.
* Add the [b-em-repo](https://github.com/stardot/b-em.git) as an upstream
  remote:
  * `git remote add upstream https://github.com/stardot/b-em.git &&
    git fetch upstream`
* Create a topic-branch to house your work
* Rebase it against `upstream/master`
* Push the latest changes to your fork;
* Open a pull-request

Once a pull-request is opened, someone from the b-em development team will
take a look at it.

Alternatively, if pull-requests are not an option, then `git-send-email` can be
used, sending the relevant patchsets to the current b-em maintainer.

### Merging changes / Pull Requests

The history of `master` should be as linear as possible, therefore when
merging changes to it the branch(es) in question should be rebased against
master first of all.  This will stop a merge commit from happening.

If using github this process is easy, since the `Merge pull request` button
has an option to `Rebase and Merge`.  This is what should be used.  See also
[the documentation on Github](https://github.com/blog/2243-rebase-and-merge-pull-requests)

This can be done manually:

```
git checkout topic/branch
git rebase origin/master
git checkout master
git merge topic/branch
git push
```
