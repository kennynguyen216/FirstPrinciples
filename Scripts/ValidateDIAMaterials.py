import json
from pathlib import Path
import time
import numpy as np
from PIL import Image
from GenerateDIAFixtures import header, light, mesh
from ValidateDIA import ROOT, environment, fingerprint, freeze, luminance, read_pfm, run

output=ROOT/'Intermediate/DIAEvidence'/(time.strftime('%Y%m%d-%H%M%S')+'-materials')
output.mkdir(parents=True)
frozen,_=freeze(output)
textures=frozen/'Content/Textures/Validation'
textures.mkdir(parents=True)
patterns={
 'Color':np.array([[[255,128,32],[32,64,192]],[[64,224,96],[192,32,128]]],dtype=np.uint8),
 'Normal':np.array([[[160,96,240],[112,160,248]],[[144,144,248],[96,112,240]]],dtype=np.uint8),
 'Tilt':np.full((2,2,3),(218,128,218),dtype=np.uint8),
}
for name,values in {'Roughness':[[64,192],[255,128]],'Metallic':[[0,128],[255,64]],'Specular':[[64,255],[128,192]],'AO':[[64,128],[192,255]]}.items():
 patterns[name]=np.repeat(np.array(values,dtype=np.uint8)[...,None],3,axis=-1)
for name,pixels in patterns.items(): Image.fromarray(pixels).save(textures/f'{name}.png')
materials=frozen/'Content/Materials/DIA'
(materials/'Mapped.material').write_text('Shader "Lit"\nBaseColor 0.8 0.7 0.6 1\nMetallic 0.6\nRoughness 0.8\nSpecular 0.7\nNormalStrength 0.8\nAmbientOcclusionStrength 0.7\nTiling 3 -2\nOffset 0.37 -0.23\nTwoSided true\n'+''.join(f'{field} "Textures/Validation/{name}.png"\n' for field,name in [('BaseColorMap','Color'),('NormalMap','Normal'),('RoughnessMap','Roughness'),('MetallicMap','Metallic'),('SpecularMap','Specular'),('AmbientOcclusionMap','AO')]))
(materials/'Tilt.material').write_text('Shader "Lit"\nBaseColor 1 1 1 1\nSpecular 0\nNormalMap "Textures/Validation/Tilt.png"\n')
scenes=frozen/'Content/Scenes/DIA'
for name,blocks in {
 'Mapped':header()+[mesh('Mapped receiver','0 0 0','-2 2 1','Mapped',rotation='15 25 0'),light(position='2 2 3')],
 'BackLight':header()+[mesh('Tilted receiver','0 0 0','100 100 1','Tilt',shape='Quad'),light(position='3 0 -1')],
 'BackLightNear':header()+[mesh('Tilted receiver','0 0 0','100 100 1','Tilt',shape='Quad'),light(position='0.0001 0 -0.00005')],
 'TiltSky':header(1)+[mesh('Tilted receiver','0 0 0','100 100 1','Tilt',shape='Quad')],
}.items(): (scenes/f'{name}.scene').write_text('version 2\n\n'+'\n\n'.join(blocks)+'\n')
hashes=fingerprint(frozen)
runtime=frozen/'Binaries/Release/Runtime.exe'
checks={}
for name in ('Mapped','BackLight','BackLightNear'):
 size=33 if name=='BackLightNear' else 64
 a,b=output/f'{name}.pfm',output/f'{name}-reference.pfm'
 run(runtime,[f'Scenes/DIA/{name}.scene','--width',size,'--height',size,'--internal-height',size,'--frames',64,'--warmup',0,'--viewmode','direct','--hdr',a,'--reference',b,'--reference-size',size,'--reference-spp',1,'--reference-bounces',1,'--metrics',output/f'{name}.json'],output/f'{name}.log')
 candidate,reference=read_pfm(a),read_pfm(b)
 mean=float(luminance(reference).mean())
 error=float(np.sqrt(np.mean((candidate-reference)**2)))/max(mean,1e-5)
 checks[name]={'status':'PASS' if error<=0.01 else 'FAIL','relative_rmse':error,'reference_mean':mean}
run(runtime,['Scenes/DIA/TiltSky.scene','--width',64,'--height',64,'--internal-height',64,'--frames',600,'--warmup',300,'--viewmode','diffuse','--hdr',output/'TiltSky.pfm','--metrics',output/'TiltSky.json'],output/'TiltSky.log')
normal=np.array([218,128,218])/255*2-1
expected=(1+normal[2]/np.linalg.norm(normal))/2
candidate=read_pfm(output/'TiltSky.pfm')
mean_error=abs(float(candidate.mean())/expected-1)
rmse=float(np.sqrt(np.mean((candidate-expected)**2)))/expected
checks['geometric_hemisphere']={'status':'PASS' if mean_error<=0.02 and rmse<=0.05 else 'FAIL','expected':float(expected),'candidate_mean':float(candidate.mean()),'relative_mean_error':mean_error,'relative_rmse':rmse}
assert fingerprint(frozen)==hashes
report={'source_hashes':hashes,'environment':environment(),'checks':checks,'independent_review':'PENDING'}
(output/'results.json').write_text(json.dumps(report,indent=2)+'\n')
(output/'run.py').write_text(Path(__file__).read_text())
print(checks,flush=True)
print('Evidence:',output,flush=True)
