import argparse
import json
import re
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
ENGINE_CONTENT = ROOT / "Engine/Content" if (ROOT / "Engine/Content").is_dir() else ROOT / "Content/Engine"
TRACE_BUFFERS = ["NodeBuffer", "TriangleBuffer", "InstanceBuffer", "MaterialBuffer",
                 "LightBuffer", "TexelBuffer", "SurfaceBuffer", "EmitterBuffer"]
PASSES = {"DIACache": (1, TRACE_BUFFERS), "DIATrace": (1, TRACE_BUFFERS), "DIATraceLitOne": (1, TRACE_BUFFERS),
          "DIAHistory": (11, ["InstanceBuffer", "MaterialBuffer"]),
          "DIADenoise": (8, ["InstanceBuffer", "MaterialBuffer"]), "DIACompose": (3, []), "DIAResolve": (8, TRACE_BUFFERS, {0}),
          "DIAResolveFallback": (8, TRACE_BUFFERS), "DIAResolveFallbackLit": (8, TRACE_BUFFERS)}

RAY_PASSES = ("DIACache", "DIATrace", "DIATraceLitOne", "DIAResolve", "DIAResolveFallback", "DIAResolveFallbackLit")


def check_bindings(reflection, metal, samplers, buffers, optional=frozenset(), ray_query=False):
    acceleration = reflection.get("acceleration_structures", [])
    assert len(acceleration) == int(ray_query)
    assert all(item["set"] == 4 and item["binding"] == 0 for item in acceleration)
    uniforms = reflection.get("ubos", [])
    assert len(uniforms) == 1 and uniforms[0]["set"] == 3 and uniforms[0]["binding"] == 0
    textures = reflection.get("textures", [])
    bindings = {item["binding"] for item in textures}
    assert len(textures) == len(bindings) and set(range(samplers)) - set(optional) <= bindings <= set(range(samplers))
    assert all(item["set"] == 2 for item in textures)
    expected_metal = {"DIAData": 0}
    for item in reflection.get("ssbos", []):
        index = buffers.index(item["name"])
        assert item["set"] == 2 and item["binding"] == samplers + index
        expected_metal[item["name"]] = 1 + index
    if metal is not None:
        observed = dict((name, int(index)) for name, index in re.findall(
            r"(\w+)&\s+\w+\s+\[\[buffer\((\d+)\)\]\]", metal))
        assert observed == expected_metal, (observed, expected_metal)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--self-check", action="store_true")
    args = parser.parse_args()
    sample = {"ubos": [{"set": 3, "binding": 0}], "textures": [{"set": 2, "binding": 0}],
              "ssbos": [{"name": "InstanceBuffer", "set": 2, "binding": 1}]}
    metal = "constant DIAData& data [[buffer(0)]], const device InstanceBuffer& instances [[buffer(1)]]"
    check_bindings(sample, metal, 1, ["InstanceBuffer"])
    try:
        check_bindings(sample, metal.replace("buffer(1)", "buffer(11)"), 1, ["InstanceBuffer"])
    except AssertionError:
        pass
    else:
        raise AssertionError("Accepted an incompatible Metal storage binding")
    mirror = {"ubos": sample["ubos"], "textures": [{"set": 2, "binding": index} for index in range(8)],
              "ssbos": [{"name": name, "set": 2, "binding": 8 + index} for index, name in enumerate(("InstanceBuffer", "MaterialBuffer"))]}
    metal = "constant DIAData& data [[buffer(0)]], const device InstanceBuffer& instances [[buffer(1)]], const device MaterialBuffer& materials [[buffer(2)]]"
    check_bindings(mirror, metal, 8, ["InstanceBuffer", "MaterialBuffer"])
    fallback = {"ubos": sample["ubos"], "textures": [{"set": 2, "binding": index} for index in range(8)],
                "ssbos": [{"name": name, "set": 2, "binding": 8 + index} for index, name in enumerate(TRACE_BUFFERS)]}
    metal = "constant DIAData& data [[buffer(0)]]" + "".join(f", const device {name}& value{index} [[buffer({index + 1})]]" for index, name in enumerate(TRACE_BUFFERS))
    check_bindings(fallback, metal, 8, TRACE_BUFFERS)
    ray = dict(sample, acceleration_structures=[{"set": 4, "binding": 0}])
    check_bindings(ray, None, 1, ["InstanceBuffer"], ray_query=True)
    for acceleration in ([], [{"set": 3, "binding": 0}], [{"set": 4, "binding": 1}],
                         [{"set": 4, "binding": 0}, {"set": 4, "binding": 0}]):
        try:
            check_bindings(dict(sample, acceleration_structures=acceleration), None, 1, ["InstanceBuffer"], ray_query=True)
        except AssertionError:
            pass
        else:
            raise AssertionError("Accepted an incompatible acceleration structure binding")
    try:
        check_bindings(ray, metal, 1, ["InstanceBuffer"])
    except AssertionError:
        pass
    else:
        raise AssertionError("Accepted a ray query descriptor in a software shader")
    if args.self_check:
        print("Resource binding self-check PASS")
        return
    for name, (samplers, buffers, *optional) in PASSES.items():
        path = ENGINE_CONTENT / "Shaders" / (name + ".frag")
        result = subprocess.run(["spirv-cross", str(path) + ".spv", "--reflect"],
                                capture_output=True, text=True, check=True)
        reflection = json.loads(result.stdout)
        if name == "DIADenoise":
            assert {item["name"] for item in reflection.get("ssbos", [])} == set(buffers)
        check_bindings(reflection, Path(str(path) + ".msl").read_text(), samplers, buffers, *optional)
        print(name, "PASS Vulkan and Metal resource bindings")
    for name in RAY_PASSES:
        samplers, buffers, *optional = PASSES[name]
        path = ENGINE_CONTENT / "Shaders" / (name + "RT.frag.spv")
        result = subprocess.run(["spirv-cross", str(path), "--reflect"], capture_output=True, text=True, check=True)
        check_bindings(json.loads(result.stdout), None, samplers, buffers, *optional, ray_query=True)
        print(name + "RT", "PASS Vulkan ray query resource bindings")


if __name__ == "__main__":
    main()
