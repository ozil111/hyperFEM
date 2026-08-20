import subprocess
import sys
import os
import platform
import argparse
import time
import threading
import glob
import shutil
import locale
from pathlib import Path

def run_script(script_path, args=[], log_file=None, show_time=False):
    """Runs a script and handles output, logging, and errors."""
    start_time = time.time()
    elapsed_time = 0
    timer_thread = None
    process = None

    # --- FIX ---
    # Detect the system's default console encoding to correctly handle localized
    # output from command-line tools (e.g., error messages in Chinese).
    # Fallback to 'utf-8' if the encoding can't be determined.
    console_encoding = locale.getpreferredencoding(False) or 'utf-8'
    print(f"Using console encoding: {console_encoding}")


    # Timer function to display elapsed time
    def print_elapsed_time():
        nonlocal elapsed_time
        while process and process.poll() is None:
            elapsed_time = time.time() - start_time
            minutes, seconds = divmod(elapsed_time, 60)
            print(f"\rElapsed time: {int(minutes)}m {int(seconds)}s", end="")
            time.sleep(1)

    try:
        # Start the script execution
        command = [script_path] + args
        print(f"Executing: {' '.join(command)}") # Log the command being run

        # --- FIX ---
        # Use the detected console_encoding for both the log file and the subprocess pipe.
        if log_file:
            with open(log_file, 'w', encoding=console_encoding, errors='replace') as log:
                process = subprocess.Popen(command, text=True, stdout=log, stderr=log, encoding=console_encoding)
        else:
            process = subprocess.Popen(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, encoding=console_encoding, errors='replace')

        if show_time:
            timer_thread = threading.Thread(target=print_elapsed_time)
            timer_thread.daemon = True
            timer_thread.start()

        stdout, stderr = process.communicate()
        
        if not log_file:
            if stdout:
                print(stdout)
            if stderr:
                # Print stderr only if the process failed
                if process.returncode != 0:
                    print(stderr, file=sys.stderr)

        if process.returncode != 0:
            # Manually raise an error to be caught by the except block
            raise subprocess.CalledProcessError(process.returncode, command, stdout, stderr)

    except (subprocess.CalledProcessError, FileNotFoundError) as e:
        # Stop the timer thread if it's running
        if timer_thread and timer_thread.is_alive():
            # The process is already finished, so the timer will stop on its own
            pass
        
        elapsed_time = time.time() - start_time
        minutes, seconds = divmod(elapsed_time, 60)
        
        print(f"\n--- SCRIPT FAILED ---", file=sys.stderr)
        print(f"Error running {script_path}", file=sys.stderr)
        print(f"Failed in {int(minutes)}m {int(seconds)}s", file=sys.stderr)

        if log_file:
            print(f"Check the log file for details: {log_file}", file=sys.stderr)
            # The log file is already written, no need to append.
        else:
            if hasattr(e, 'stderr') and e.stderr:
                print(f"Error details:\n{e.stderr}", file=sys.stderr)

        # Create a 'bad.build' file to indicate failure
        with open(os.path.join('build', 'bad.build'), 'w', encoding='utf-8') as f:
            f.write(f"Build failed in {int(minutes)}m {int(seconds)}s\n")
        sys.exit(e.returncode if hasattr(e, 'returncode') else 1)
    
    else:
        # Success case
        if timer_thread and timer_thread.is_alive():
            # The process is already finished, so the timer will stop on its own
            pass
            
        elapsed_time = time.time() - start_time
        minutes, seconds = divmod(elapsed_time, 60)
        print(f"\n--- SCRIPT SUCCEEDED ---")
        print(f"Completed successfully in {int(minutes)}m {int(seconds)}s")
        
        # Create a 'good.build' file to indicate success
        with open(os.path.join('build', 'good.build'), 'w', encoding='utf-8') as f:
            f.write(f"Build completed successfully in {int(minutes)}m {int(seconds)}s\n")

def get_project_venv_python(project_root: str) -> str:
    """Return python executable from .venv if it exists, otherwise fallback to current interpreter."""
    project_root_path = Path(project_root)
    if platform.system() == "Windows":
        venv_python = project_root_path / ".venv" / "Scripts" / "python.exe"
    else:
        venv_python = project_root_path / ".venv" / "bin" / "python"

    if venv_python.is_file():
        print(f"Using virtual environment Python at: {venv_python}")
        return str(venv_python)

    # Fallback: current interpreter
    print("Warning: .venv not found, falling back to current Python interpreter.")
    return sys.executable


def run_integration_tests(project_root: str, mode: str, show_time: bool = False):
    """Run integration tests by delegating to test_case/test.py as a thin wrapper.

    The solver path (bin/Debug vs bin/Release) is resolved inside test.py based
    on the ``--mode`` argument, keeping this wrapper as thin as possible.
    """
    start_time = time.time()
    elapsed_time = 0
    timer_thread = None
    process = None

    # For integration tests, force UTF-8 so symbols like '✓' don't break on GBK consoles.
    console_encoding = 'utf-8'
    print(f"Using integration console encoding: {console_encoding}")

    def print_elapsed_time():
        nonlocal elapsed_time
        while process and process.poll() is None:
            elapsed_time = time.time() - start_time
            minutes, seconds = divmod(elapsed_time, 60)
            print(f"\r[Integration] Elapsed time: {int(minutes)}m {int(seconds)}s", end="")
            time.sleep(1)

    project_root_path = Path(project_root)
    test_script = project_root_path / "test_case" / "test.py"
    python_exe = get_project_venv_python(project_root)

    # Map build mode to solver variant: debug-ish modes use Debug, release-ish use Release.
    if mode in ('release', 'gcc-release', 'msvc-release'):
        solver_mode = 'release'
    else:
        solver_mode = 'debug'

    command = [python_exe, str(test_script), '--mode', solver_mode,
               '--history-dir', os.path.join('.symtest', solver_mode)]
    print(f"Executing integration tests: {' '.join(command)}")

    # Ensure the child Python process uses UTF-8 for stdio to avoid UnicodeEncodeError with '✓'
    # and to prevent mojibake when subprocesses print Chinese text (e.g. '[√] 运行完成').
    env = os.environ.copy()
    env.setdefault("PYTHONIOENCODING", "utf-8")
    env.setdefault("PYTHONUTF8", "1")

    try:
        process = subprocess.Popen(
            command,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            encoding=console_encoding,
            errors='replace',
            env=env,
            cwd=str(project_root_path),
        )

        if show_time:
            timer_thread = threading.Thread(target=print_elapsed_time)
            timer_thread.daemon = True
            timer_thread.start()

        # Stream merged stdout/stderr (stderr merged into stdout to avoid pipe deadlock)
        output_lines = []
        while True:
            line = process.stdout.readline()
            if not line and process.poll() is not None:
                break
            if line:
                print(line.rstrip())
                output_lines.append(line)

        return_code = process.wait()
        if return_code != 0:
            raise subprocess.CalledProcessError(return_code, command, ''.join(output_lines))

    except (subprocess.CalledProcessError, FileNotFoundError) as e:
        elapsed_time = time.time() - start_time
        minutes, seconds = divmod(elapsed_time, 60)

        print(f"\n--- INTEGRATION TESTS FAILED ---", file=sys.stderr)
        print(f"Error running integration tests", file=sys.stderr)
        print(f"Failed in {int(minutes)}m {int(seconds)}s", file=sys.stderr)

        if hasattr(e, 'output') and e.output:
            print(f"Error details:\n{e.output}", file=sys.stderr)

        sys.exit(e.returncode if hasattr(e, 'returncode') else 1)

    else:
        elapsed_time = time.time() - start_time
        minutes, seconds = divmod(elapsed_time, 60)
        print(f"\n--- INTEGRATION TESTS SUCCEEDED ---")
        print(f"Integration tests completed successfully in {int(minutes)}m {int(seconds)}s")

def main():
    parser = argparse.ArgumentParser(description="Build and Test Script Runner")
    parser.add_argument('--build', action='store_true', help="Run the build script")
    parser.add_argument('--itest', action='store_true', help="Run integration tests in test_case using .venv if available")
    parser.add_argument(
        '--mode',
        choices=['debug', 'release', 'gcc', 'gcc-release', 'msvc', 'msvc-release'],
        default='debug',
        help="Build mode: debug/release = Clang (default toolchain); gcc/gcc-release = MinGW GCC",
    )
    parser.add_argument('--rebuild', action='store_true', help="Clean build directories before building")
    
    args = parser.parse_args()

    # Determine preset name based on mode
    if args.mode == 'msvc':
        preset_name = 'msvc'
    elif args.mode == 'msvc-release':
        preset_name = 'msvc_release'
    elif args.mode == 'release':
        preset_name = 'release'
    elif args.mode == 'gcc':
        preset_name = 'gcc'
    elif args.mode == 'gcc-release':
        preset_name = 'gcc-release'
    else:  # debug (Clang)
        preset_name = 'default'

    build_args = [f'--{args.mode}']
    
    # The --rebuild logic is now handled entirely within this Python script
    # by deleting the directory, so we no longer pass a --clean flag down.

    log_file = os.path.join('build', 'log.txt')

    if args.build:
        # Ensure the top-level 'build' directory exists
        os.makedirs('build', exist_ok=True)

        # Delete log.txt and all .build files before the run
        if os.path.exists(log_file):
            try:
                os.remove(log_file)
            except OSError as e:
                print(f"Warning: Could not remove {log_file}: {e}")
        
        for build_file in glob.glob(os.path.join('build', '*.build')):
            try:
                os.remove(build_file)
            except OSError as e:
                print(f"Warning: Could not remove {build_file}: {e}")

        # If --rebuild is specified, clean the correct build directory
        if args.rebuild:
            # Use the determined preset_name to find the correct directory to delete.
            build_dir = os.path.join('build', preset_name)
            
            print(f"Rebuild requested: Cleaning directory '{build_dir}'...")
            if os.path.exists(build_dir):
                try:
                    shutil.rmtree(build_dir)
                    print(f"Removed directory: {build_dir}")
                except OSError as e:
                    print(f"Warning: Could not remove {build_dir}: {e}")
            else:
                print(f"Directory {build_dir} does not exist, nothing to clean.")

        print("Running build script...")
        script_name = 'build.bat' if platform.system() == "Windows" else 'build.sh'
        run_script(os.path.join('build_scripts', script_name), build_args, log_file, show_time=True)
        print(f"Build log saved to {log_file}")

    if args.itest:
        # Use project root for resolving .venv and test_case
        project_root = os.path.abspath('.')
        print("Running integration tests (test_case)...")

        run_integration_tests(project_root, mode=args.mode, show_time=True)

if __name__ == "__main__":
    main()
