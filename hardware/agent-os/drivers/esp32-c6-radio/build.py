from pathlib import Path
from prepare_radio_sdk import prepare as prepare_radio

def link(context, properties):
    output = context["native_build"] / "radio"
    response = prepare_radio(Path(properties["compiler.sdk.path"]), Path(properties["compiler.path"]), output)
    return {"compiler.c.elf.libs": "@" + str(response),
            "build.zigbee_libs": "-lesp_zb_api.zczr -lzboss_stack.zczr " + str(output / "libzboss_port.native.a")}
