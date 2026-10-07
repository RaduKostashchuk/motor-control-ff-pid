import csv
import matplotlib.pyplot as plt

# ==== НАСТРОЙКИ ====
INPUT_FILE = 'src/scripts/out/telemetry.csv'
TARGET_RPM = 50          # уставка, которую подавали

# ==== ЧТЕНИЕ CSV ====
t = []
desired = []
traj = []
current = []
error = []
p = []
i = []
d = []
uff = []
pwm = []

with open(INPUT_FILE, 'r', encoding='utf-8') as f:
    reader = csv.DictReader(f)
    for row in reader:
        t.append(float(row['t_ms']) / 1000.0)  # в секунды
        desired.append(float(row['desired']))
        traj.append(float(row['traj']))
        current.append(float(row['current']))
        error.append(float(row['error']))
        p.append(float(row['p']))
        i.append(float(row['i']))
        d.append(float(row['d']))
        uff.append(float(row['uff']))
        pwm.append(float(row['pwm']))

# Сдвигаем время: t=0 в момент скачка
t0 = None
for idx, val in enumerate(traj):
    if val > 0:
        t0 = t[idx]
        break
if t0 is not None:
    t = [x - t0 for x in t]

print(f"Точек: {len(t)}")
print(f"Длительность: {t[-1]:.3f} сек")

# ==== ГРАФИК 1: СКОРОСТЬ ====
fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(12, 8), sharex=True)

ax1.plot(t, desired, label='desired', linestyle='--', color='gray')
ax1.plot(t, traj, label='traj', linestyle=':', color='orange')
ax1.plot(t, current, label='current', color='blue')
ax1.axhline(y=TARGET_RPM, color='red', linestyle='--', alpha=0.3, label=f'target={TARGET_RPM}')
ax1.set_ylabel('RPM')
ax1.set_title('Скорость: desired / traj / current')
ax1.grid(True, alpha=0.3)
ax1.legend()

# ==== ГРАФИК 2: УПРАВЛЕНИЕ ====
ax2.plot(t, pwm, label='pwm', color='black')
ax2.plot(t, p, label='p', color='green')
ax2.plot(t, i, label='i', color='red')
ax2.plot(t, d, label='d', color='purple')
ax2.plot(t, uff, label='uff', color='brown')
ax2.set_xlabel('Время, сек')
ax2.set_ylabel('Значение')
ax2.set_title(f'Компоненты PID + feed-forward. Average error = {round(sum(error) / len(error), 2)}')
ax2.grid(True, alpha=0.3)
ax2.legend()

plt.tight_layout()
plt.savefig('src/scripts/images/step_response.png', dpi=100)
print("График сохранён в step_response.png")
plt.show()