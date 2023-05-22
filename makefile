all: dmabufshare

dmabufshare: main.c socket.h window.h render.h
	$(CC) main.c -g -L /usr/lib/aarch64-linux-gnu/pvr/ -lEGL -lGLESv2 -lX11 -lm -lgbm -o dmabufshare

clean:
	rm -f dmabufshare
