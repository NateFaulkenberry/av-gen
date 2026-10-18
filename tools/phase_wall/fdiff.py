import sys, subprocess, tempfile, glob, numpy as np
from PIL import Image
# fdiff.py <mp4> [crop w:h:x:y] : mean |frame(t+1)-frame(t)| in grey levels, p50 and p95 over the clip
mp4=sys.argv[1]; crop=sys.argv[2] if len(sys.argv)>2 else None
d=tempfile.mkdtemp()
vf=("crop=%s,"%crop if crop else "")+"scale=960:-1"
subprocess.run(["ffmpeg","-loglevel","error","-i",mp4,"-vf",vf,d+"/%04d.png"],check=True)
a=[np.asarray(Image.open(x).convert('L'),float) for x in sorted(glob.glob(d+"/*.png"))]
v=np.array([np.abs(a[i+1]-a[i]).mean() for i in range(len(a)-1)])
print("%s frames=%d diff p50=%.1f p95=%.1f max=%.1f"%(mp4.split('/')[-1],len(a),np.median(v),np.percentile(v,95),v.max()))
