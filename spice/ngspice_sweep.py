"""
F6645M301GP — corrected topology, full frequency sweep with diagnostic plots.
"""
import subprocess, numpy as np, re, os
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

OUTDIR = '/home/claude/spice/output'
os.makedirs(OUTDIR, exist_ok=True)

def make_netlist(fsw_hz, ltank_uH, lpri_mH=3, nps=25, duration_ms=4.0):
    return f"""* F6645M301GP corrected — fsw={fsw_hz/1e3:.1f}kHz

.param Vbus=325
.param Cres=0.18u
.param Ltank={ltank_uH}u
.param Lpri={lpri_mH}m
.param Nps={nps}
.param fsw={fsw_hz}
.param dt=1u
.param Vgate=15
.param Tsw='1/fsw'
.param Th='Tsw/2 - dt'

Vbus pos 0 DC {{Vbus}}
Cbus pos 0 10u

.model SW_IGBT SW(Vt=2 Vh=0.5 Ron=0.05 Roff=10Meg)
.model DIDE D(Is=1n Cjo=300p Rs=0.05 BV=1200)

Shi  pos sw  ghi sw  SW_IGBT
Dhi  sw  pos DIDE
Slo  sw  0   glo 0   SW_IGBT
Dlo  0   sw  DIDE

Vghi ghi sw  PULSE(0 {{Vgate}} 0       50n 50n {{Th}} {{Tsw}})
Vglo glo 0   PULSE(0 {{Vgate}} {{Tsw/2}} 50n 50n {{Th}} {{Tsw}})

Llk    sw   a    {{Ltank}}
Cres   a    b    {{Cres}}
Lpri   b    0    {{Lpri}}
Lsec   sec  secrtn  'Lpri*Nps*Nps'
K1     Lpri Lsec   0.995
Rgnd_s secrtn 0   1Meg

.model DHV D(Is=10n N=2 Rs=20 BV=15kV Cjo=15p TT=200n)
Dd1    sec    hv     DHV
C704   hv     secrtn 8.2n
Dd2    secrtn gnd2   DHV
C705   gnd2   sec    5.6n

.model DMAG D(Is=1n N=8 Rs=200 BV=4000 Cjo=20p TT=10n)
Dmag   hv    mag    DMAG
Rmag   mag   gnd2   800
Rbleed hv 0  100Meg

.options reltol=1e-3 abstol=1u vntol=1m method=gear maxstep=100n

.control
tran 100n {duration_ms}m uic
write /tmp/sim_out.raw v(sw) v(a) v(b) i(Llk) v(hv) v(gnd2) v(sec) v(ghi,sw) v(glo)
quit
.endc
.end
"""

def run_sim(fsw_hz, ltank_uH, duration_ms=4.0):
    with open('/tmp/sim.cir','w') as f:
        f.write(make_netlist(fsw_hz, ltank_uH, duration_ms=duration_ms))
    subprocess.run(['ngspice','-b','/tmp/sim.cir'], capture_output=True, text=True, timeout=180)
    return parse_raw('/tmp/sim_out.raw')

def parse_raw(path):
    with open(path,'rb') as f:
        d = f.read()
    hdr_end = d.find(b'Binary:')
    header = d[:hdr_end].decode('latin-1')
    binary = d[hdr_end + len(b'Binary:\n'):]
    nvars = int(re.search(r'No\. Variables:\s*(\d+)', header).group(1))
    npoints = int(re.search(r'No\. Points:\s*(\d+)', header).group(1))
    names = re.findall(r'^\s*\d+\s+(\S+)\s+\S+', header, re.MULTILINE)
    arr = np.frombuffer(binary[:npoints*nvars*8], dtype=np.float64).reshape(npoints, nvars)
    return {names[i]: arr[:, i] for i in range(nvars)}

# Frequency sweep
freqs_kHz = [15, 18, 20, 22, 25, 28, 32, 36, 40, 45, 50]
ltank_uH = 250

print(f"Sweep at Ltank={ltank_uH}µH, Lpri=3mH, Nps=25")
print(f"{'fsw':>6s} {'Vsw_pp':>10s} {'I_tank_rms':>12s} {'V_HV_avg':>12s} {'Vsw_at_Q701_on':>15s}")
print('-'*70)

results = {}
for f_kHz in freqs_kHz:
    fsw = f_kHz * 1000
    data = run_sim(fsw, ltank_uH, 4.0)
    if data is None: print(f"  {f_kHz} kHz: sim failed"); continue
    
    t = data['time']
    mask = t > 3e-3
    if mask.sum() < 100: print(f"  {f_kHz} kHz: too few samples"); continue
    vsw = data['v(sw)'][mask]
    itank = data['i(llk)'][mask]
    vhv = data['v(hv)'][mask] - data['v(gnd2)'][mask]
    
    # ZVS check: find V(sw) at the moments Q701 gate goes high
    glo = data['v(glo)'][mask]
    glo_rising = np.where((glo[:-1] < 2) & (glo[1:] > 2))[0]
    if len(glo_rising) > 0:
        vsw_at_turnon = np.mean(vsw[glo_rising])
    else:
        vsw_at_turnon = np.nan
    
    vsw_pp = vsw.max() - vsw.min()
    itank_rms = np.sqrt(np.mean(itank**2))
    vhv_avg = vhv.mean()
    
    results[f_kHz] = dict(vsw_pp=vsw_pp, itank_rms=itank_rms, vhv_avg=vhv_avg,
                          vsw_at_turnon=vsw_at_turnon, data=data)
    print(f"{f_kHz:>5d}k {vsw_pp:>9.1f}V {itank_rms:>11.2f}A {vhv_avg:>11.0f}V {vsw_at_turnon:>14.1f}V")

f_r_calc = 1/(2*np.pi*np.sqrt(250e-6 * 0.18e-6))/1000
print(f"\nCalculated f_r = {f_r_calc:.2f} kHz (from L={ltank_uH}µH, C=0.18µF)")
print(f"Real f_r is lower due to Lpri also in series (effective L = Ltank + reflected Lpri)")

# === Plot 1: sweep summary ===
freqs = sorted(results.keys())
fig, axes = plt.subplots(3, 1, figsize=(10, 9))

vsw_pps = [results[f]['vsw_pp'] for f in freqs]
itank_rmss = [results[f]['itank_rms'] for f in freqs]
vhv_avgs = [abs(results[f]['vhv_avg']) for f in freqs]
vsw_at_turnons = [results[f]['vsw_at_turnon'] for f in freqs]

axes[0].plot(freqs, itank_rmss, 'o-', color='C1', lw=2)
axes[0].axvline(f_r_calc, color='g', ls='--', alpha=0.5, label=f'f_r calc = {f_r_calc:.1f}kHz')
axes[0].set_ylabel('Tank current RMS (A)')
axes[0].set_title('Tank current — peak = LC resonance')
axes[0].legend(); axes[0].grid(alpha=0.3)

axes[1].plot(freqs, vhv_avgs, '^-', color='C2', lw=2)
axes[1].axhline(4000, color='r', ls=':', alpha=0.5, label='Magnetron strike (4 kV)')
axes[1].set_ylabel('|HV output average| (V)')
axes[1].set_title('HV doubler output')
axes[1].legend(); axes[1].grid(alpha=0.3)

axes[2].plot(freqs, vsw_at_turnons, 'D-', color='C3', lw=2)
axes[2].axhline(0, color='g', ls=':', label='Ideal ZVS (V=0 at turn-on)')
axes[2].axhline(325, color='r', ls=':', alpha=0.5, label='Hard switching (V=Vbus)')
axes[2].set_ylabel('V(sw) at Q701 turn-on (V)')
axes[2].set_xlabel('Drive frequency f_sw (kHz)')
axes[2].set_title('ZVS check: V(switch_node) at the instant Q701 gate turns on')
axes[2].legend(); axes[2].grid(alpha=0.3)

plt.suptitle(f'F6645M301GP frequency sweep — corrected topology\nC701=0.18µF, Ltank={ltank_uH}µH', fontsize=12)
plt.tight_layout()
plt.savefig(f'{OUTDIR}/sweep_corrected.png', dpi=110, bbox_inches='tight')
plt.close()
print(f"\nSaved {OUTDIR}/sweep_corrected.png")

# === Plot 2: waveforms at three operating points ===
# Pick: well-below, near-resonance, well-above
zvs_changes = [(f, results[f]['vsw_at_turnon']) for f in freqs]
# Find lowest-frequency ZVS point and a clearly above-resonance point
picks = [15, 22, 36]  # below, near, above
fig, axes = plt.subplots(3, 3, figsize=(15, 10))

for col, f_kHz in enumerate(picks):
    if f_kHz not in results: continue
    data = results[f_kHz]['data']
    t = data['time']
    cycles_t = 4/(f_kHz*1000)
    t_start = t[-1] - cycles_t
    mask = (t > t_start)
    t_us = (t[mask] - t_start) * 1e6
    
    axes[0, col].plot(t_us, data['v(sw)'][mask], 'b-', lw=1.5, label='V(sw)')
    axes[0, col].axhline(325, color='r', ls='--', alpha=0.4)
    axes[0, col].axhline(0, color='k', lw=0.5)
    axes[0, col].set_ylabel('V(sw) [V]')
    axes[0, col].set_title(f'{f_kHz} kHz drive')
    axes[0, col].grid(alpha=0.3)
    
    axes[1, col].plot(t_us, data['i(llk)'][mask], 'g-', lw=1.5)
    axes[1, col].axhline(0, color='k', lw=0.5)
    axes[1, col].set_ylabel('I(tank) [A]')
    axes[1, col].grid(alpha=0.3)
    
    vhv_inst = data['v(hv)'][mask] - data['v(gnd2)'][mask]
    axes[2, col].plot(t_us, vhv_inst, 'm-', lw=1.5)
    axes[2, col].axhline(-4000, color='r', ls='--', alpha=0.4, label='Magnetron strike')
    axes[2, col].set_ylabel('V(HV) [V]')
    axes[2, col].set_xlabel('Time (µs)')
    axes[2, col].legend(fontsize=8); axes[2, col].grid(alpha=0.3)

labels = ['LOW freq', f'~{int(f_r_calc)} kHz', 'HIGH freq']
for col, lbl in enumerate(labels):
    axes[0, col].text(0.02, 0.98, lbl, transform=axes[0,col].transAxes,
                      va='top', fontweight='bold', fontsize=11,
                      bbox=dict(facecolor='yellow', alpha=0.7))

plt.suptitle('F6645M301GP waveforms — corrected topology', fontsize=12)
plt.tight_layout()
plt.savefig(f'{OUTDIR}/waveforms_corrected.png', dpi=110, bbox_inches='tight')
plt.close()
print(f"Saved {OUTDIR}/waveforms_corrected.png")
