################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (14.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Src/adc.c \
../Src/dma.c \
../Src/ecg_rpeak.c \
../Src/gpio.c \
../Src/i2c.c \
../Src/lcd_module.c \
../Src/main.c \
../Src/max30102.c \
../Src/pwv_metrics.c \
../Src/rng.c \
../Src/spi.c \
../Src/stm32f7xx_hal_msp.c \
../Src/stm32f7xx_it.c \
../Src/syscalls.c \
../Src/sysmem.c \
../Src/system_stm32f7xx.c \
../Src/tim.c \
../Src/usart.c 

OBJS += \
./Src/adc.o \
./Src/dma.o \
./Src/ecg_rpeak.o \
./Src/gpio.o \
./Src/i2c.o \
./Src/lcd_module.o \
./Src/main.o \
./Src/max30102.o \
./Src/pwv_metrics.o \
./Src/rng.o \
./Src/spi.o \
./Src/stm32f7xx_hal_msp.o \
./Src/stm32f7xx_it.o \
./Src/syscalls.o \
./Src/sysmem.o \
./Src/system_stm32f7xx.o \
./Src/tim.o \
./Src/usart.o 

C_DEPS += \
./Src/adc.d \
./Src/dma.d \
./Src/ecg_rpeak.d \
./Src/gpio.d \
./Src/i2c.d \
./Src/lcd_module.d \
./Src/main.d \
./Src/max30102.d \
./Src/pwv_metrics.d \
./Src/rng.d \
./Src/spi.d \
./Src/stm32f7xx_hal_msp.d \
./Src/stm32f7xx_it.d \
./Src/syscalls.d \
./Src/sysmem.d \
./Src/system_stm32f7xx.d \
./Src/tim.d \
./Src/usart.d 


# Each subdirectory must supply rules for building sources it contributes
Src/%.o Src/%.su Src/%.cyclo: ../Src/%.c Src/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m7 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32F746xx -c -I../Inc -I"D:/Work/MAD/Final/MAD_Final/Src/ILI9341" -I../Drivers/CMSIS/Device/ST/STM32F7xx/Include -I../Drivers/CMSIS/Include -I"D:/Work/MAD/Final/MAD_Final/Drivers/STM32F7xx_HAL_Driver/Inc" -I"D:/Work/MAD/Final/MAD_Final/Drivers/STM32F7xx_HAL_Driver/Inc/Legacy" -I../Drivers/STM32F7xx_HAL_Driver/Inc -I../Drivers/STM32F7xx_HAL_Driver/Inc/Legacy -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-sp-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-Src

clean-Src:
	-$(RM) ./Src/adc.cyclo ./Src/adc.d ./Src/adc.o ./Src/adc.su ./Src/dma.cyclo ./Src/dma.d ./Src/dma.o ./Src/dma.su ./Src/ecg_rpeak.cyclo ./Src/ecg_rpeak.d ./Src/ecg_rpeak.o ./Src/ecg_rpeak.su ./Src/gpio.cyclo ./Src/gpio.d ./Src/gpio.o ./Src/gpio.su ./Src/i2c.cyclo ./Src/i2c.d ./Src/i2c.o ./Src/i2c.su ./Src/lcd_module.cyclo ./Src/lcd_module.d ./Src/lcd_module.o ./Src/lcd_module.su ./Src/main.cyclo ./Src/main.d ./Src/main.o ./Src/main.su ./Src/max30102.cyclo ./Src/max30102.d ./Src/max30102.o ./Src/max30102.su ./Src/pwv_metrics.cyclo ./Src/pwv_metrics.d ./Src/pwv_metrics.o ./Src/pwv_metrics.su ./Src/rng.cyclo ./Src/rng.d ./Src/rng.o ./Src/rng.su ./Src/spi.cyclo ./Src/spi.d ./Src/spi.o ./Src/spi.su ./Src/stm32f7xx_hal_msp.cyclo ./Src/stm32f7xx_hal_msp.d ./Src/stm32f7xx_hal_msp.o ./Src/stm32f7xx_hal_msp.su ./Src/stm32f7xx_it.cyclo ./Src/stm32f7xx_it.d ./Src/stm32f7xx_it.o ./Src/stm32f7xx_it.su ./Src/syscalls.cyclo ./Src/syscalls.d ./Src/syscalls.o ./Src/syscalls.su ./Src/sysmem.cyclo ./Src/sysmem.d ./Src/sysmem.o ./Src/sysmem.su ./Src/system_stm32f7xx.cyclo ./Src/system_stm32f7xx.d ./Src/system_stm32f7xx.o ./Src/system_stm32f7xx.su ./Src/tim.cyclo ./Src/tim.d ./Src/tim.o ./Src/tim.su ./Src/usart.cyclo ./Src/usart.d ./Src/usart.o ./Src/usart.su

.PHONY: clean-Src

