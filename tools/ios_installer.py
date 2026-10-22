#!/usr/bin/env python3
"""iOS Miatoll installer (macOS): asks for the IPSW, builds the iOS root from it and flashes the phone.

  python3 tools/ios_installer.py            window
  python3 tools/ios_installer.py --cli IPSW  the same without a window

Steps: 1. the IPSW of iPhone X (iPhone10,3) 16.7.16 is chosen and checked;
       2. tools/ios-install.sh builds the root in a Terminal window (it needs your Mac's password there);
       3. with the phone in fastboot mode, userdata and boot are flashed.
The IPSW is Apple's: it is downloaded by whoever installs, from Apple or from where they got it.
Installing erases Android's data partition (userdata) on the phone. Keep a copy of your data first.
By Samrat
"""
import glob
import os
import shutil
import subprocess
import sys
import threading
import time
import urllib.request
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
WORK = os.path.join(os.path.expanduser('~'), 'ios-miatoll-work')
NEED = ('087-86698-044.dmg', '087-86593-047.dmg', '087-86715-047.dmg')
PLATFORM_TOOLS = 'https://dl.google.com/android/repository/platform-tools-latest-darwin.zip'


def check_ipsw(path):
    """(ok, message): a zip that holds the three disk images of iPhone X 16.7.16."""
    if not path or not os.path.isfile(path):
        return False, 'Файл не найден.'
    try:
        with zipfile.ZipFile(path) as z:
            names = set(z.namelist())
    except zipfile.BadZipFile:
        return False, 'Это не IPSW (файл не открывается как zip).'
    missing = [n for n in NEED if n not in names]
    if missing:
        return False, 'Нужен IPSW для iPhone X (iPhone10,3) 16.7.16 (20H392). Нет файлов: ' + ', '.join(missing)
    return True, 'IPSW подходит.'


def find_fastboot():
    path = shutil.which('fastboot')
    if path:
        return path
    local = os.path.join(WORK, 'platform-tools', 'fastboot')
    return local if os.path.isfile(local) else None


def get_fastboot(log):
    """fastboot from Google's platform-tools, if the Mac has none."""
    os.makedirs(WORK, exist_ok=True)
    zpath = os.path.join(WORK, 'platform-tools.zip')
    log('Скачиваю fastboot (platform-tools) с dl.google.com ...')
    urllib.request.urlretrieve(PLATFORM_TOOLS, zpath)
    with zipfile.ZipFile(zpath) as z:
        z.extractall(WORK)
    for exe in glob.glob(os.path.join(WORK, 'platform-tools', '*')):
        os.chmod(exe, 0o755)
    return find_fastboot()


def find_boot_image():
    for base in (ROOT, os.path.dirname(ROOT), os.getcwd(), os.path.expanduser('~/Downloads')):
        hits = sorted(glob.glob(os.path.join(base, 'ios-miatoll-boot*.img')))
        if hits:
            return hits[-1]
    return None


def build_in_terminal(ipsw, log):
    """Run tools/ios-install.sh in Terminal (the Mac's password is typed there) and wait until it is done."""
    os.makedirs(WORK, exist_ok=True)
    done = os.path.join(WORK, 'build.done')
    if os.path.exists(done):
        os.remove(done)
    script = os.path.join(WORK, 'build.command')
    with open(script, 'w') as f:
        f.write('#!/bin/bash\n')
        f.write('cd "%s"\n' % ROOT)
        f.write('echo "iOS Miatoll: сборка iOS из IPSW. Введите пароль Mac, когда спросит."\n')
        f.write('tools/ios-install.sh "%s" "%s" && echo ok > "%s" || echo failed > "%s"\n' % (ipsw, WORK, done, done))
        f.write('echo; echo "Готово. Это окно можно закрыть."\n')
    os.chmod(script, 0o755)
    subprocess.run(['open', '-a', 'Terminal', script], check=False)
    log('Открыто окно Terminal: сборка идёт там (долго, около 14 ГБ). Жду...')
    while not os.path.exists(done):
        time.sleep(3)
    return open(done).read().strip() == 'ok'


def wait_fastboot(fastboot, log, seconds=120):
    log('Переведите телефон в fastboot (выключить, затем Power + Vol−) и подключите кабель ...')
    end = time.time() + seconds
    while time.time() < end:
        out = subprocess.run([fastboot, 'devices'], capture_output=True, text=True).stdout
        if 'fastboot' in out:
            return True
        time.sleep(2)
    return False


def flash(fastboot, simg, boot, log):
    for part, img in (('userdata', simg), ('boot', boot)):
        log('Прошиваю %s: %s' % (part, os.path.basename(img)))
        p = subprocess.run([fastboot, 'flash', part, img], capture_output=True, text=True)
        log((p.stdout + p.stderr).strip()[-600:])
        if p.returncode != 0:
            return False
    log('Перезагружаю телефон ...')
    subprocess.run([fastboot, 'reboot'], capture_output=True)
    return True


def cli(ipsw):
    ok, msg = check_ipsw(ipsw)
    print(msg)
    if not ok:
        return 1
    if input('Android на телефоне будет стёрт. Продолжить? (да/нет) ') != 'да':
        return 1
    if not build_in_terminal(ipsw, print):
        print('Сборка не удалась, смотрите окно Terminal.')
        return 1
    fastboot = find_fastboot() or get_fastboot(print)
    boot = find_boot_image()
    if not boot or not fastboot:
        print('Нет fastboot или образа ios-miatoll-boot*.img рядом.')
        return 1
    if not wait_fastboot(fastboot, print):
        print('Телефон в fastboot не найден.')
        return 1
    return 0 if flash(fastboot, os.path.join(WORK, 'ios16-root.simg'), boot, print) else 1


def gui():
    import tkinter as tk
    from tkinter import filedialog, messagebox, scrolledtext

    win = tk.Tk()
    win.title('iOS Miatoll: установка')
    win.geometry('640x560')
    win.configure(bg='#000000')
    state = {'ipsw': None, 'busy': False}
    FG, DIM, ACC = '#f5f5f7', '#86868b', '#2997ff'

    tk.Label(win, text='iOS Miatoll', font=('Helvetica Neue', 34, 'bold'), fg=FG, bg='#000000').pack(pady=(24, 0))
    tk.Label(win, text='Redmi Note 9S', font=('Helvetica Neue', 15), fg=DIM, bg='#000000').pack()
    tk.Label(win, text='Установка iOS на телефон', font=('Helvetica Neue', 15), fg=DIM, bg='#000000').pack(pady=(0, 14))

    status = tk.Label(win, text='Шаг 1. Выберите IPSW для iPhone X 16.7.16', fg=FG, bg='#000000', font=('Helvetica Neue', 14), wraplength=580)
    status.pack(pady=(4, 8))
    box = scrolledtext.ScrolledText(win, height=14, bg='#0b0b0d', fg='#d2d2d7', insertbackground='#d2d2d7', relief='flat', font=('Menlo', 11))
    box.pack(fill='both', expand=True, padx=20, pady=8)

    def log(text):
        def put():
            box.insert('end', text + '\n')
            box.see('end')
        win.after(0, put)

    agree = tk.IntVar(value=0)
    tk.Checkbutton(win, text='Я понимаю: Android на телефоне будет стёрт, копии данных сделаны', variable=agree,
                   fg=FG, bg='#000000', selectcolor='#000000', activebackground='#000000', activeforeground=FG).pack()

    buttons = tk.Frame(win, bg='#000000')
    buttons.pack(pady=10)
    choose = tk.Button(buttons, text='1. Выбрать IPSW…', width=18)
    start = tk.Button(buttons, text='2. Установить', width=18, state='disabled')
    choose.grid(row=0, column=0, padx=6)
    start.grid(row=0, column=1, padx=6)
    tk.Label(win, text='By Samrat', fg=DIM, bg='#000000', font=('Helvetica Neue', 12)).pack(pady=(0, 12))

    def set_status(text):
        win.after(0, lambda: status.config(text=text))

    def pick():
        path = filedialog.askopenfilename(title='IPSW iPhone X 16.7.16', filetypes=[('IPSW', '*.ipsw'), ('Все файлы', '*')])
        if not path:
            return
        ok, msg = check_ipsw(path)
        log(msg)
        if ok:
            state['ipsw'] = path
            start.config(state='normal')
            set_status('Шаг 2. Нажмите «Установить»')
        else:
            messagebox.showerror('IPSW', msg)

    def run():
        if state['busy']:
            return
        if not agree.get():
            messagebox.showwarning('Подтверждение', 'Поставьте галочку: установка стирает Android на телефоне.')
            return
        state['busy'] = True
        start.config(state='disabled')

        def work():
            try:
                set_status('Сборка iOS из IPSW (в окне Terminal) ...')
                if not build_in_terminal(state['ipsw'], log):
                    set_status('Сборка не удалась. Смотрите окно Terminal.')
                    return
                fastboot = find_fastboot() or get_fastboot(log)
                boot = find_boot_image()
                if not fastboot:
                    set_status('Не удалось получить fastboot.')
                    return
                if not boot:
                    log('Не найден файл ios-miatoll-boot*.img рядом с программой.')
                    set_status('Положите ios-miatoll-boot*.img рядом и повторите.')
                    return
                set_status('Переведите телефон в fastboot (Power + Vol−) и подключите кабель')
                if not wait_fastboot(fastboot, log):
                    set_status('Телефон в fastboot не найден.')
                    return
                set_status('Прошиваю телефон ...')
                ok = flash(fastboot, os.path.join(WORK, 'ios16-root.simg'), boot, log)
                set_status('Готово: телефон перезагружается в iOS Miatoll.' if ok else 'Прошивка не удалась, смотрите журнал.')
            except Exception as exc:    # shown to the person installing
                log('Ошибка: %s' % exc)
                set_status('Ошибка: %s' % exc)
            finally:
                state['busy'] = False
                win.after(0, lambda: start.config(state='normal'))

        threading.Thread(target=work, daemon=True).start()

    choose.config(command=pick)
    start.config(command=run)
    log('Программа собирает iOS из вашего IPSW и ставит его на телефон.')
    log('Файлов Apple здесь нет: IPSW скачивается отдельно (например, ipsw.me/iPhone10,3).')
    win.mainloop()


if __name__ == '__main__':
    if sys.platform != 'darwin':
        sys.exit('Сборка iOS работает только на macOS.')
    if len(sys.argv) >= 3 and sys.argv[1] == '--cli':
        sys.exit(cli(sys.argv[2]))
    gui()
