CC      = gcc
CFLAGS  = -Wall -Wextra -g
LDFLAGS = -lglut -lGLU -lGL -lm

TARGET  = traffic
SRCS    = main.c config.c shared_state.c traffic_names.c \
          logger.c light.c vehicle.c pedestrian.c \
          emergency.c control.c display.c
OBJS    = $(SRCS:.c=.o)

.PHONY: all clean run install-deps

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(OBJS) $(LDFLAGS) -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

run: all
	./$(TARGET) config.txt

install-deps:
	sudo apt-get install -y gcc make freeglut3-dev

clean:
	rm -f $(OBJS) $(TARGET) traffic.log
