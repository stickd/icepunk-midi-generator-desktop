import test from "node:test";
import { Client, neonConfig } from "@neondatabase/serverless";
import { activationRaces } from "./activation-postgres.mjs";

test("Neon: real independent-connection activation and admin races", {
  skip: !process.env.TEST_DATABASE_URL && "Set TEST_DATABASE_URL to a migrated dedicated Neon test branch.",
  timeout: 90000,
}, async () => {
  neonConfig.webSocketConstructor = globalThis.WebSocket;
  await activationRaces(async name => {
    const client=new Client({connectionString:process.env.TEST_DATABASE_URL});
    await client.connect();
    await client.query("SELECT set_config('application_name',$1,false)",[name]);
    return client;
  });
});

