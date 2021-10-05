With integer keys, it is easier to multiplex multiple RAM accesses because we can predict if two or more accesses can be neighbors, hence local multiplexing is possible. 

But global multiplexing would require more advanced features of C++ such as nano-coroutines but this would solve the "integer keys only" problem.

Example local multiplexing:

Layout in the circular buffers (mapped by unordered_map)

```
Cache-line width = 4
          cache line 0       cache line 1         cache line 2        cache line 3
index     0  1  2  3         4  5   6   7        8  9   10   11       12   13  14   15
          ^  ^ 2x data              ^ 1x data         1x data              3x data
           \/                      /
            \                    /
             \                 /
            "1x find"       "1x find"
                \          /   
                 \__      /     
                  \ \   /   
user requests: key 0,1,6,10,12,13,14 = 4x unordered_map find operations, 7x data
```
