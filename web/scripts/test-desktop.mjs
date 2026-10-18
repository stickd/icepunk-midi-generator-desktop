// Test-only harness; never packaged. Requires a disposable signer matching the test build.
import { createPublicKey } from "node:crypto";
import { readFileSync, writeFileSync, mkdtempSync } from "node:fs";
import { join, resolve } from "node:path";
import { spawn } from "node:child_process";
import { createServer } from "node:http";
import { signingKey, authorization } from "../lib/authorization.mjs";
const [exe, privateFile, work, mode] = process.argv.slice(2);
if (!work) throw new Error("Usage: node --conditions=react-server scripts/test-desktop.mjs EXE disposable-private.env WORK_DIR [--http]");
const secret=readFileSync(privateFile,"utf8").trim().split("=").slice(1).join("=");
const key=signingKey({LICENSE_SIGNING_PRIVATE_KEY:secret});
const pk=createPublicKey(key).export({format:"der",type:"spki"}).subarray(-32).toString("hex");
const dir=mkdtempSync(join(resolve(work),"desktop-test-"));
const run=(...extra)=>new Promise((resolve,reject)=>{const p=spawn(exe,[dir,pk,...extra],{stdio:"inherit",windowsHide:true});p.on("error",reject);p.on("exit",code=>code===0?resolve():reject(new Error("Desktop test failed: "+code)));});
await run();
const device=readFileSync(join(dir,"device.txt"),"utf8").trim();
const metadata={licenseId:"00000000-0000-4000-8000-000000000001",instagram:"@integration_test",licenseType:"standard"};
writeFileSync(join(dir,"token.txt"),authorization(metadata,device,key));
await run();
if(mode==="--http") {
 let response="success";
 const server=createServer(async(req,res)=>{
  let data="";for await(const chunk of req)data+=chunk;
  const body=JSON.parse(data);
  if(req.url!=="/api/license/activate" || req.method!=="POST" || body.deviceId!==device) {res.writeHead(400).end();return;}
  res.setHeader("Content-Type","application/json");
  if(response==="success")res.end(JSON.stringify({ok:true,authorization:authorization(metadata,device,key)}));
  else {res.statusCode=response==="DEVICE_LIMIT_REACHED"?409:403;res.end(JSON.stringify({ok:false,code:response}));}
 });
 await new Promise(resolve=>server.listen(0,"127.0.0.1",resolve));
 try {for(const state of ["INVALID_LICENSE","LICENSE_REVOKED","DEVICE_LIMIT_REACHED","success"]){response=state;await run("http://127.0.0.1:"+server.address().port,state);console.log("PASS native HTTP "+state);}}
 finally {await new Promise(resolve=>server.close(resolve));}
}
console.log("Disposable signed fixture for packaged tests: "+join(dir,"token.txt"));
