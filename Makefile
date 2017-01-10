all:
	clang -Wall -O3 -g -std=c99 -o luafb luafb.c -I/usr/include/lua5.2 -I/usr/include/freetype2 -llua5.2 -lfreetype

test:
	valgrind ./luafb test.lua
