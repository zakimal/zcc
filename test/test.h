#define ASSERT(x, y) assert(x, y, #y)

int assert(int expected, int actual, char *code);
int printf(char *fmt, ...);
int sprintf(char *buf, char *fmt, ...);
int strcmp(char *p, char *q);
int str_cmp(char *p1, char *p2)
{
    for (; *p1 == *p2; p1++, p2++)
    {
        if (*p1 == '\0')
            return 0;
    }
    return *p1 - *p2;
}
int memcmp(char *p, char *q, long n);