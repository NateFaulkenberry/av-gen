import numpy as np, wave, matplotlib; matplotlib.use('Agg'); import matplotlib.pyplot as plt
def read_wav(path):
    with wave.open(path,"rb") as w: ch,sw,sr,n=w.getnchannels(),w.getsampwidth(),w.getframerate(),w.getnframes(); raw=w.readframes(n)
    b=np.frombuffer(raw,np.uint8).reshape(-1,3); x=(b[:,0].astype(np.int32)|(b[:,1].astype(np.int32)<<8)|(b[:,2].astype(np.int32)<<16))
    x=np.where(x>=1<<23,x-(1<<24),x).astype(np.float32)/float(1<<23); return x.reshape(-1,ch).mean(1),sr
def env(k,sec=0.5):
    x,sr=read_wav('assets/audio/stems/feline-footwear (%s).wav'%k); hop=int(sr*0.02); n=len(x)//hop
    r=np.sqrt((x[:n*hop].reshape(n,hop)**2).mean(1)+1e-12); kk=int(sec/0.02); r=np.convolve(r,np.ones(kk)/kk,'same')
    return np.arange(n)*0.02, 20*np.log10(r+1e-9)
d=np.load('build/rwf/synth_env.npz'); tc=d['tc']; hi=np.convolve(d['hi'],np.ones(6)/6,'same')
bar=lambda n: 0.2425+(n-1)*1.98347
SW=[ # name, onset, peak, end, scale(0-1), kind
 ("call",34.0,35.8,37.1,0.45,"local"),("drop",47.85,48.7,50.2,1.0,"full"),("56",56.2,56.6,58.5,0.5,"wide"),
 ("1:04",64.8,66.0,66.8,0.35,"local"),("1:12",72.3,73.9,74.6,0.4,"local"),("1:20",80.2,81.4,82.5,0.45,"wide"),
 ("1:28",88.6,89.8,90.4,0.35,"local"),("1:36",96.4,97.0,98.0,0.3,"local"),("1:44",104.1,105.7,106.3,0.45,"wide"),
 ("1:47-1:51",107.3,112.9,113.7,0.95,"full"),("1:59",117.8,118.1,119.6,0.4,"local"),("2:07",125.7,126.5,127.6,0.45,"wide"),
 ("2:15",133.8,134.2,135.5,0.4,"local"),("2:23",143.8,144.6,146.4,0.5,"wide"),("2:31",151.2,153.2,154.0,0.6,"wide"),
 ("2:39",157.0,161.7,163.7,1.0,"full"),("2:47",166.5,167.0,168.5,0.5,"wide"),("2:53",171.0,172.3,173.2,0.45,"wide")]
secs=[(0.24,8.18,"Intro: drums"),(8.18,20.1,"bass pedal"),(20.1,47.85,"synth calls / build"),(47.85,111.32,"Groove A (drop)"),
      (111.32,150.99,"Groove B (16th hats)"),(150.99,166.86,"lift: brightest"),(166.86,182.72,"pedal, darkening"),(182.72,194.6,"drums stop: riser"),(194.6,210.5,"synth alone: fade")]
fig,ax=plt.subplots(4,1,figsize=(26,12),sharex=True,gridspec_kw=dict(height_ratios=[1,1,1,0.6]))
for i,(k,c) in enumerate((('Drums','C0'),('Bass','C3'),('Synth','C2'))):
    t,v=env(k); ax[i].plot(t,v,lw=.6,c=c); ax[i].set_ylabel(k+' dB'); ax[i].set_ylim(-50,-5)
b2=ax[2].twinx(); b2.plot(tc,hi,c='k',lw=.5,alpha=.6); b2.set_ylabel('synth >2 kHz share')
for n,o,p,e,s,kd in SW:
    col={'full':'r','wide':'orange','local':'gold'}[kd]
    ax[2].axvspan(o,e,color=col,alpha=.25); ax[2].text(p,-7,n,ha='center',fontsize=8)
    ax[3].bar(p,s,width=e-o,color=col,alpha=.8)
ax[3].set_ylabel('scan scale'); ax[3].set_ylim(0,1.15)
for a in ax:
    for k in range(1,107,4): a.axvline(bar(k),c='k',lw=.35,alpha=.4)
for x0,x1,n in secs:
    ax[0].axvspan(x0,x1,color='C%d'%(hash(n)%9),alpha=.07); ax[0].text((x0+x1)/2,-9,n,ha='center',fontsize=9)
ax[3].axvspan(173,210.5,color='purple',alpha=.15); ax[3].text(191,1.0,'ending: bit crush, mosh, distortion, landing, fade',ha='center')
ax[3].set_xticks(range(0,212,8)); ax[3].set_xticklabels(['%d:%02d'%(x//60,x%60) for x in range(0,212,8)])
plt.xlim(0,211); plt.tight_layout(); plt.savefig('build/rwf/structure.png',dpi=60)
