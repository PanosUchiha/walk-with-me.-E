"""Summarize immutable captured handholding traces with timestamps and line numbers."""
import json,re,statistics,sys
from pathlib import Path
path=Path(sys.argv[1]);lines=path.read_text(encoding='utf-8-sig',errors='replace').splitlines()
rows=[];events=[]
for n,line in enumerate(lines,1):
    if '[HandHolding]' not in line:continue
    if 'Sticky ' in line:
        row={k:float(v) for k,v in re.findall(r'(\w+)=(-?\d+(?:\.\d+)?)',line)}
        row.update(line=n,time=line[1:24]);rows.append(row)
    elif any(word in line for word in ('Attached','Released','Revision','guard installed')):
        events.append({'line':n,'text':line})
report={'source':str(path.resolve()),'samples':len(rows),'movingSamples':sum(r['desiredSpeed']>5 for r in rows),
    'zeroPhysicsWhileMoving':sum(r['desiredSpeed']>5 and r['physicsSpeed']==0 for r in rows),
    'physicsMismatchSamples':sum(abs(r['desiredSpeed']-r['physicsSpeed'])>.2 for r in rows),
    'events':events,'largestCorrections':sorted(rows,key=lambda r:r['correction'],reverse=True)[:6]}
for key in ('correction','wristGap','headingError','maxNativeHeadingErrorDegrees','nativeHeadingWrites'):
    values=[r[key] for r in rows]
    report[key]={'min':min(values),'median':statistics.median(values),'max':max(values)}
output=path.parent/'handholding-summary.json';output.write_text(json.dumps(report,indent=2))
print(json.dumps({k:v for k,v in report.items() if k not in ('events','largestCorrections')},indent=2))
