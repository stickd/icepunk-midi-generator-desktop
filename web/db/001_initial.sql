CREATE TABLE invites (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  token_hash text NOT NULL UNIQUE CHECK (token_hash ~ '^[a-f0-9]{64}$'),
  instagram_username varchar(30) NOT NULL CHECK (instagram_username ~ '^[a-z0-9._]{1,30}$'),
  license_type text NOT NULL CHECK (license_type IN ('standard', 'beta')),
  max_devices integer NOT NULL DEFAULT 2 CHECK (max_devices BETWEEN 1 AND 20),
  created_at timestamptz NOT NULL DEFAULT now(),
  used_at timestamptz,
  expires_at timestamptz,
  CHECK (used_at IS NULL OR used_at >= created_at),
  CHECK (expires_at IS NULL OR expires_at > created_at)
);
CREATE TABLE licenses (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  invite_id uuid NOT NULL UNIQUE REFERENCES invites(id),
  email varchar(254) NOT NULL,
  instagram_username varchar(30) NOT NULL CHECK (instagram_username ~ '^[a-z0-9._]{1,30}$'),
  key_hash text NOT NULL UNIQUE CHECK (key_hash ~ '^[a-f0-9]{64}$'),
  key_suffix varchar(8) NOT NULL,
  license_type text NOT NULL CHECK (license_type IN ('standard', 'beta')),
  status text NOT NULL DEFAULT 'active' CHECK (status IN ('active', 'revoked')),
  max_devices integer NOT NULL DEFAULT 2 CHECK (max_devices BETWEEN 1 AND 20),
  created_at timestamptz NOT NULL DEFAULT now()
);
CREATE TABLE devices (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  license_id uuid NOT NULL REFERENCES licenses(id),
  fingerprint text NOT NULL CHECK (length(fingerprint) BETWEEN 1 AND 256),
  created_at timestamptz NOT NULL DEFAULT now(),
  UNIQUE (license_id, fingerprint)
);
CREATE TABLE feedback (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  license_id uuid REFERENCES licenses(id) ON DELETE SET NULL,
  instagram_username varchar(30),
  app_version varchar(64),
  category text NOT NULL CHECK (category IN ('bug', 'idea', 'feedback')),
  message text NOT NULL CHECK (length(btrim(message)) BETWEEN 1 AND 5000),
  created_at timestamptz NOT NULL DEFAULT now()
);
CREATE INDEX invites_created_at_idx ON invites (created_at DESC);
CREATE INDEX feedback_license_id_idx ON feedback (license_id);
CREATE TABLE request_limits (
  key text PRIMARY KEY,
  attempts integer NOT NULL CHECK (attempts > 0),
  reset_at timestamptz NOT NULL
);

