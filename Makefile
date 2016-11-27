CFLAGS += -Wall -MMD
LDFLAGS += -lusb-1.0 -lSDL2

TARGET = ds-stream
OBJS = $(patsubst %.c,%.o,$(wildcard *.c))

app: $(TARGET)

clean:
	rm -f $(OBJS) *.d $(TARGET)

$(TARGET): $(OBJS)
	$(CC) -o $(TARGET) $^ $(LDFLAGS)

-include $(patsubst %.o,%.d,$(OBJS))

.c.o:
	$(CC) $(CFLAGS) -c -o $@ $<

.PHONY: app clean

