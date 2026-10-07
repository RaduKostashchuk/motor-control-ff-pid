import serial
import time

# ==== НАСТРОЙКИ ====
PORT = 'COM5'
BAUD = 115200
DURATION = 10          # сколько секунд писать
OUTPUT_FILE = 'src/scripts/out/telemetry.csv'
TARGET_RPM = 50

# ==== ОТКРЫТИЕ ПОРТА ====
ser = serial.Serial(PORT, BAUD, timeout=1)
print(f"Открыт порт {PORT} на скорости {BAUD}")
print("Жду 2 секунды пока Arduino стабилизируется")
time.sleep(2)

# ==== СТАРТ МОТОРА ====
print("Отправляю MODE AUTO")
ser.write(b'MODE AUTO\n')
print(f"Отправляю MOTOR 1 {TARGET_RPM}")
ser.write(f'MOTOR 1 {TARGET_RPM}\n'.encode())

# ==== СБОР ДАННЫХ ====
print(f"Собираю телеметрию {DURATION} секунд")
start = time.time()
lines = 0
skipped = 0

with open(OUTPUT_FILE, 'w', encoding='utf-8') as f:
    # Заголовок
    f.write("t_ms,desired,traj,current,error,p,i,d,uff,pwm\n")
    
    while time.time() - start < DURATION:
        line = ser.readline().decode('utf-8', errors='ignore').strip()
        if not line:
            continue
        if not line.startswith("T:"):
            skipped += 1
            continue
	# Фильтр: только строки, которые парсятся как 10 чисел
        values = []
        try:
            values = [float(v) for v in line[2:].split(',')]
        except ValueError:
            skipped +=1
            continue
        if len(values) != 10:
            skipped += 1
            continue
        f.write(f"{line[2:]}\n")
        lines += 1

# ==== ОСТАНОВКА МОТОРА ====
print("Отправляю STOP 1")
ser.write(b'STOP 1\n')

ser.close()
print(f"Готово. Записано {lines} строк в {OUTPUT_FILE}")
print(f"Пропущено {skipped} строк")
