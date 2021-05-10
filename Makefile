CFLAGS += -fPIC
CFLAGS += -I./qemu/include/qemu

all: libsystem_cover.so

lib%.so: %.o
	$(CC) -shared -Wl,-soname,$@ -o $@ $^ $(LDLIBS)
