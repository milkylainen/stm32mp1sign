AM_CFLAGS = -std=c99 -Wall -Wextra -Wshadow

bin_PROGRAMS = stm32mp1sign
stm32mp1sign_SOURCES = stm32mp1sign.c

stm32mp1sign_CFLAGS = $(AM_CFLAGS) $(CRYPTO_CFLAGS)
stm32mp1sign_CPPFLAGS = $(AM_CPPFLAGS) $(CRYPTO_CPPFLAGS)
stm32mp1sign_LDADD = $(CRYPTO_LIBS)
