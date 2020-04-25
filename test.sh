#!/bin/bash
assert() {
    expected="$1"
    input="$2"

    ./zcc "$input" > tmp.s
    cc -o tmp tmp.s
    ./tmp
    actual="$?"

    if [ "$actual" = "$expected" ]; then
        echo "$input => $actual"
    else
        echo "$input => $expected expected, but got $actual"
        exit 1
    fi
}

echo
echo -e '\033[1;37mtesting simple arithmetics...\033[0;39m'
assert 0 'return 0;'
assert 42 'return 42;'
assert 21 'return 5+20-4;'
assert 41 'return  12 + 34 - 5 ;'
assert 47 'return 5+6*7;'
assert 15 'return 5*(9-6);'
assert 4 'return (3+5)/2;'
assert 10 'return -10+20;'
assert 10 'return - -10;'
assert 10 'return - - +10;'

echo
echo -e '\033[1;37mtesting equalities...\033[0;39m'
assert 0 'return 0==1;'
assert 1 'return 42==42;'
assert 1 'return 0!=1;'
assert 0 'return 42!=42;'

echo
echo -e '\033[1;37mtesting comparisons...\033[0;39m'
assert 1 'return 0<1;'
assert 0 'return 1<1;'
assert 0 'return 2<1;'
assert 1 'return 0<=1;'
assert 1 'return 1<=1;'
assert 0 'return 2<=1;'

assert 1 'return 1>0;'
assert 0 'return 1>1;'
assert 0 'return 1>2;'
assert 1 'return 1>=0;'
assert 1 'return 1>=1;'
assert 0 'return 1>=2;'

echo
echo -e '\033[1;37mtesting single-letter variables...\033[0;39m'
assert 3 'a=3; return a;'
assert 8 'a=3; z=5; return a+z;'

echo
echo -e '\033[1;37mtesting multiple statements...\033[0;39m'
assert 1 'return 1; 2; 3;'
assert 2 '1; return 2; 3;'
assert 3 '1; 2; return 3;'

echo
echo -e '\033[1;37mtesting multi-letter variables...\033[0;39m'
assert 3 'foo=3; return foo;'
assert 8 'foo123=3; bar=5; return foo123+bar;'

echo
echo -e '\033[1;37mtesting if-else statements...\033[0;39m'
assert 3 'if (0) return 2; return 3;'
assert 3 'if (1-1) return 2; return 3;'
assert 2 'if (1) return 2; return 3;'
assert 2 'if (2-1) return 2; return 3;'

echo
echo -e '\033[1;37mtesting while statements...\033[0;39m'
assert 10 'i=0; while(i<10) i=i+1; return i;'

echo
echo -e '\033[1;32mPASS!\033[0;39m'