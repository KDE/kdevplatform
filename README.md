KDevPlatform has been moved into kdevelop.git:kdevplatform/

For copying private branches over to the other repo, use copy-branch.sh:
```
cd kdevplatform
cp copy-branch.sh.in copy-branch.sh

# now open copy-branch.sh, adapt the 3 variables at the begin and save the file

# with that done, ready to go:
# call the script once for each branch that should be copied over
./copy-branch my-branch
```
