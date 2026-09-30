<<<<<<< HEAD
ccflags-y += -O3

obj-$(CONFIG_LZ4_COMPRESS) += lz4_compress.o
obj-$(CONFIG_LZ4HC_COMPRESS) += lz4hc_compress.o
obj-$(CONFIG_LZ4_DECOMPRESS) += lz4_decompress.o
=======
ccflags-y += -O3 \
    -Wframe-larger-than=131072 \
    -DLZ4_FREESTANDING=1 \
    -DLZ4_FAST_DEC_LOOP=1

obj-$(CONFIG_LZ4) += lz4.o
obj-$(CONFIG_LZ4HC) += lz4hc.o
>>>>>>> origin/4.19.325
