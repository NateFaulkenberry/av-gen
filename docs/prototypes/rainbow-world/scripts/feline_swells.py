import sys, numpy as np, json
import wave
def read_wav(path):
    with wave.open(path, "rb") as w:
        ch, sw, sr, n = w.getnchannels(), w.getsampwidth(), w.getframerate(), w.getnframes()
        raw = w.readframes(n)
    b = np.frombuffer(raw, np.uint8).reshape(-1, 3)
    x = (b[:, 0].astype(np.int32) | (b[:, 1].astype(np.int32) << 8) | (b[:, 2].astype(np.int32) << 16))
    x = np.where(x >= 1 << 23, x - (1 << 24), x).astype(np.float32) / float(1 << 23)
    return x.reshape(-1, ch), sr
import matplotlib; matplotlib.use('Agg'); import matplotlib.pyplot as plt
S='assets/audio/stems/feline-footwear (%s).wav'
def load(k):
    return read_wav(S%k)
x,sr=load('Synth'); x=x.mean(1) if x.ndim>1 else x
hop=int(sr*0.02); n=len(x)//hop
fr=x[:n*hop].reshape(n,hop)
rms=np.sqrt((fr**2).mean(1)+1e-12)
t=np.arange(n)*0.02
def smooth(v,sec):
    k=max(1,int(sec/0.02)); w=np.ones(k)/k; return np.convolve(v,w,'same')
db=20*np.log10(smooth(rms,0.5)+1e-9)
# spectral: high band energy share (brightness) via FFT per 2048 window
N=4096; hopf=int(sr*0.05); idx=np.arange(0,len(x)-N,hopf); tc=idx/sr+N/2/sr
win=np.hanning(N); freqs=np.fft.rfftfreq(N,1/sr)
cent=[]; hi=[]
for i in idx:
    m=np.abs(np.fft.rfft(x[i:i+N]*win)); p=m**2
    cent.append((freqs*p).sum()/(p.sum()+1e-12)); hi.append(p[freqs>2000].sum()/(p.sum()+1e-12))
cent=smooth(np.array(cent),0.5*0.02/0.05); hi=np.array(hi)
bar=lambda n_: 0.2425+(n_-1)*1.98347
np.savez('build/rwf/synth_env.npz',t=t,db=db,tc=tc,cent=np.array(cent),hi=hi)
owner=[(34,37),(48,50),(56,58),64,72,80,88,96,104,(107,111),119,127,135,143,151,159,167,173]
fig,ax=plt.subplots(3,1,figsize=(22,10),sharex=True)
ax[0].plot(t,db,lw=.6); ax[0].set_ylim(-60,-5); ax[0].set_ylabel('synth dB (0.5 s)')
ax[1].plot(tc,cent,lw=.6,c='C1'); ax[1].set_ylabel('centroid Hz')
ax[2].plot(tc,smooth(hi,0.5),lw=.6,c='C2'); ax[2].set_ylabel('>2k share')
for a in ax:
    for k in range(1,107,4): a.axvline(bar(k),c='k',lw=.4,alpha=.4)
    for o in owner:
        if isinstance(o,tuple): a.axvspan(o[0],o[1],color='r',alpha=.15)
        else: a.axvline(o,c='r',lw=1,alpha=.6)
ax[2].set_xticks(range(0,212,8)); plt.xlim(15,212); plt.tight_layout(); plt.savefig('build/rwf/swells.png',dpi=60)
