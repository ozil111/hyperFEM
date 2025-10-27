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

def run_test_script(test_args=[], show_time=False):
    """Runs the new test script from the test directory."""
    start_time = time.time()
    elapsed_time = 0
    timer_thread = None
    process = None

    # Detect console encoding
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
        # Determine the test script path based on platform
        if platform.system() == "Windows":
            test_script = os.path.join('test', 'run_tests.bat')
        else:
            test_script = os.path.join('test', 'run_tests.sh')
            # Make sure the script is executable
            os.chmod(test_script, 0o755)

        # The project root is the directory containing this Python script.
        # This is more reliable than having the batch/shell script guess its location.
        project_root = os.path.dirname(os.path.abspath(__file__))

        command = [os.path.abspath(test_script)] + test_args
        print(f"Executing test script from '{project_root}': {' '.join(command)}")

        # Start the test process, setting the correct working directory
        process = subprocess.Popen(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, 
                                 encoding=console_encoding, errors='replace', cwd=project_root)

        if show_time:
            timer_thread = threading.Thread(target=print_elapsed_time)
            timer_thread.daemon = True
            timer_thread.start()

        # Read output in real-time
        test_output = []
        while True:
            line = process.stdout.readline()
            if not line and process.poll() is not None:
                break
            if line:
                print(line.rstrip())
                test_output.append(line)
        
        return_code = process.wait()
        
        if return_code != 0:
            stderr = process.stderr.read()
            if stderr:
                print(f"\nTest script stderr:\n{stderr}", file=sys.stderr)
            raise subprocess.CalledProcessError(return_code, command, ''.join(test_output), stderr)

    except (subprocess.CalledProcessError, FileNotFoundError) as e:
        elapsed_time = time.time() - start_time
        minutes, seconds = divmod(elapsed_time, 60)
        
        print(f"\n--- TEST SCRIPT FAILED ---", file=sys.stderr)
        print(f"Error running test script", file=sys.stderr)
        print(f"Failed in {int(minutes)}m {int(seconds)}s", file=sys.stderr)
        
        if hasattr(e, 'stderr') and e.stderr:
            print(f"Error details:\n{e.stderr}", file=sys.stderr)
        
        sys.exit(e.returncode if hasattr(e, 'returncode') else 1)
    
    else:
        elapsed_time = time.time() - start_time
        minutes, seconds = divmod(elapsed_time, 60)
        print(f"\n--- TEST SCRIPT SUCCEEDED ---")
        print(f"Tests completed successfully in {int(minutes)}m {int(seconds)}s")

def main():
    parser = argparse.ArgumentParser(description="Build and Test Script Runner")
    parser.add_argument('--build', action='store_true', help="Run the build script")
    parser.add_argument('--test', action='store_true', help="Run the test script")
    parser.add_argument('--mode', choices=['debug', 'release', 'msvc', 'msvc-release'], default='debug', help="Build mode (default: debug)")
    parser.add_argument('--rebuild', action='store_true', help="Clean build directories before building")
    parser.add_argument('--test-target', type=str, default='all', help="Specify a test target to run (use 'all' to run all tests)")
    
    args = parser.parse_args()

    # Determine preset names based on mode and whether we're building tests
    if args.test:
        # Use test presets when running tests
        if args.mode == 'msvc':
            preset_name = 'test-msvc'
        elif args.mode == 'msvc-release':
            preset_name = 'test-msvc_release'
        elif args.mode == 'release':
            preset_name = 'test-release'
        else:  # debug
            preset_name = 'test-default'
    else:
        # Use regular presets when building main program
        if args.mode == 'msvc':
            preset_name = 'msvc'
        elif args.mode == 'msvc-release':
            preset_name = 'msvc_release'
        elif args.mode == 'release':
            preset_name = 'release'
        else:  # debug
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

    if args.test:
        print("Running test script...")
        test_args = [f'--{args.mode}']
        if args.test_target and args.test_target != 'all':
            test_args.append('--test-target')
            test_args.append(args.test_target)
        
        run_test_script(test_args, show_time=True)

if __name__ == "__main__":
    main()
