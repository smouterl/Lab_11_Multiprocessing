CC = gcc
CFLAGS = -Wall -Wextra -g
CPPFLAGS =
LDFLAGS =

PROGRAMS = mandel mandelmovie
MANDEL_OBJECTS = mandel.o jpegrw.o
MOVIE_OBJECTS = mandelmovie.o
DEPENDENCIES = mandel.d jpegrw.d mandelmovie.d

.PHONY: all clean

all: $(PROGRAMS)

mandel: $(MANDEL_OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $^ -ljpeg

mandelmovie: $(MOVIE_OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

# Avoid parsing stale dependency files when cleaning an interrupted or old build.
ifneq ($(MAKECMDGOALS),clean)
-include $(DEPENDENCIES)
endif

clean:
	rm -f $(MANDEL_OBJECTS) $(MOVIE_OBJECTS) $(PROGRAMS) $(DEPENDENCIES)
