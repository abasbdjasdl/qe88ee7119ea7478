from pathlib import Path
import os,plistlib,subprocess,tempfile,time
assert os.uname().sysname=='Darwin'
label='local.r16.autolog.ci'
installed=Path('/Library/LaunchDaemons/'+label+'.plist')
assert not installed.exists()
with tempfile.TemporaryDirectory(prefix='r16-managed-') as tmp:
    root=Path(tmp);signal=root/'started.txt';source=root/'job.plist'
    p=plistlib.loads(Path('recovery-autolog/managed-launcher.plist').read_bytes())
    p['Label']=label
    assert p['ProgramArguments']==['/bin/sh','-c','/bin/sleep 30; exec /bin/sh /usr/libexec/r16-autolog.sh']
    # Substitute only delay and workload: no hardware, firmware, network or reboot.
    p['ProgramArguments']=['/bin/sh','-c',f'/bin/sleep 1; printf managed > "{signal}"; /bin/sleep 10']
    p['StandardOutPath']=str(root/'stdout');p['StandardErrorPath']=str(root/'stderr')
    source.write_bytes(plistlib.dumps(p))
    subprocess.run(['sudo','install','-o','root','-g','wheel','-m','644',str(source),str(installed)],check=True)
    try:
        hook=Path('recovery-autolog/managed-rc-hook.sh').read_text().replace('/System/Library/LaunchDaemons/local.r16.autolog.plist',str(installed)).replace('system/local.r16.autolog','system/'+label)
        subprocess.run(['sudo','/bin/sh','-c',hook],check=True,timeout=10)
        for _ in range(40):
            if signal.exists():break
            time.sleep(.1)
        assert signal.read_text()=='managed','Independent job did not run after submitting shell exited'
        # A second invocation must not kill/restart an already running job.
        subprocess.run(['sudo','/bin/sh','-c',hook],check=True,timeout=10,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
        assert subprocess.run(['sudo','launchctl','print','system/'+label],stdout=subprocess.DEVNULL).returncode==0
    finally:
        subprocess.run(['sudo','launchctl','bootout','system/'+label],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
        subprocess.run(['sudo','rm',str(installed)],check=True)
print('PASS: actual macOS launchd bootstrap and already-loaded fallback; job survives submitting shell; no device/reboot operations.')
