DEVICE_SRC = device/acquisition.c\
			device/ads127l11.c\
			device/app_controller.c\
			device/app_config.c\
			device/command_protocol.c\
			device/fatfs_time.c\
			device/file_service.c\
			device/format_utils.c\
			device/range_ctrl.c\
			device/ring_buffer.c\
			device/storage_manager.c\
			device/storage_recorder.c\
			device/stream_service.c\
			device/system_config.c\
			device/task_scheduler.c\
			device/tf_slots.c\
			device/time_service.c\
			device/tcp_service.c\
			device/w5500_ll.c\


# Required include directories
DEVICE_INC = device

ALLCSRC += $(DEVICE_SRC)
ALLINC += $(DEVICE_INC)
