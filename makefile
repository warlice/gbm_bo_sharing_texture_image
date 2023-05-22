all: dmabufshare

dmabufshare: main.c socket.h window.h render.h
	$(CC) main.c -g -L /usr/lib/aarch64-linux-gnu/pvr/ -lepoxy -lX11 -lm -lgbm -o dmabufshare

clean:
	rm -f dmabufshare
