FATFS_SRC = Middlewares/Third_Party/FatFs/src/ff.c\
            Middlewares/Third_Party/FatFs/src/diskio.c\
            Middlewares/Third_Party/FatFs/src/option/unicode.c\

FATFS_INC = Middlewares/Third_Party/FatFs/src

ALLCSRC += $(FATFS_SRC)
ALLINC += $(FATFS_INC)
