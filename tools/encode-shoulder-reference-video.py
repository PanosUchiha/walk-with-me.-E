"""Encode the reviewed PNG sequences at native 30 FPS with local FFmpeg."""
import sys,subprocess,json
from pathlib import Path
repo=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(repo/'build/preview-media-deps'))
import imageio_ffmpeg
root=repo/'build/shoulder-recovery-v6'
ffmpeg=imageio_ffmpeg.get_ffmpeg_exe()
results={}
for stem,start,count in [('motion',1,196),('detail',43,87)]:
    path=root/f'{stem}-preview.mp4'
    subprocess.run([ffmpeg,'-y','-loglevel','error','-framerate','30','-start_number',str(start),'-i',str(root/f'{stem}-%03d.png'),'-frames:v',str(count),'-c:v','libx264','-crf','17','-pix_fmt','yuv420p','-movflags','+faststart',str(path)],check=True)
    subprocess.run([ffmpeg,'-v','error','-i',str(path),'-f','null','-'],check=True)
    results[stem]={'path':str(path),'fps':30,'frames':count,'bytes':path.stat().st_size,'decoded':True}
(root/'video-validation.json').write_text(json.dumps(results,indent=2))
print(json.dumps(results,indent=2))
