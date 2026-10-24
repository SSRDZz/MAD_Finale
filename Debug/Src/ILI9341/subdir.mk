################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (14.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Src/ILI9341/ILI9341_GFX.c \
../Src/ILI9341/ILI9341_STM32_Driver.c \
../Src/ILI9341/ILI9341_Touchscreen.c 

OBJS += \
./Src/ILI9341/ILI9341_GFX.o \
./Src/ILI9341/ILI9341_STM32_Driver.o \
./Src/ILI9341/ILI9341_Touchscreen.o 

C_DEPS += \
./Src/ILI9341/ILI9341_GFX.d \
./Src/ILI9341/ILI9341_STM32_Driver.d \
./Src/ILI9341/ILI9341_Touchscreen.d 


# Each subdirectory must supply rules for building sources it contributes
Src/ILI9341/%.o Src/ILI9341/%.su Src/ILI9341/%.cyclo: ../Src/ILI9341/%.c Src/ILI9341/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m7 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32F746xx -c -I../Inc -I"C:/Users/theet/Documents/Y3T1/MAD/Proj/main/Sardz/Src/ILI9341" -I../Drivers/CMSIS/Device/ST/STM32F7xx/Include -I../Drivers/CMSIS/Include -I"C:/Users/theet/Documents/Y3T1/MAD/Proj/main/Sardz/Drivers/STM32F7xx_HAL_Driver/Inc" -I"C:/Users/theet/Documents/Y3T1/MAD/Proj/main/Sardz/Drivers/STM32F7xx_HAL_Driver/Inc/Legacy" -I../Drivers/STM32F7xx_HAL_Driver/Inc -I../Drivers/STM32F7xx_HAL_Driver/Inc/Legacy -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-sp-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-Src-2f-ILI9341

clean-Src-2f-ILI9341:
	-$(RM) ./Src/ILI9341/ILI9341_GFX.cyclo ./Src/ILI9341/ILI9341_GFX.d ./Src/ILI9341/ILI9341_GFX.o ./Src/ILI9341/ILI9341_GFX.su ./Src/ILI9341/ILI9341_STM32_Driver.cyclo ./Src/ILI9341/ILI9341_STM32_Driver.d ./Src/ILI9341/ILI9341_STM32_Driver.o ./Src/ILI9341/ILI9341_STM32_Driver.su ./Src/ILI9341/ILI9341_Touchscreen.cyclo ./Src/ILI9341/ILI9341_Touchscreen.d ./Src/ILI9341/ILI9341_Touchscreen.o ./Src/ILI9341/ILI9341_Touchscreen.su

.PHONY: clean-Src-2f-ILI9341

