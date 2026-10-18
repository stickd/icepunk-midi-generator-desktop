ALTER TABLE feedback ADD COLUMN title varchar(120);
-- statement-breakpoint
CREATE TABLE tester_builds (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  version varchar(64) NOT NULL CHECK (length(btrim(version)) BETWEEN 1 AND 64),
  platform text NOT NULL CHECK (platform IN ('windows')),
  download_url text NOT NULL CHECK (download_url ~ '^https://'),
  file_name varchar(255),
  sha256 varchar(64) CHECK (sha256 IS NULL OR sha256 ~ '^[A-Fa-f0-9]{64}$'),
  release_notes text CHECK (release_notes IS NULL OR length(release_notes) <= 2000),
  created_at timestamptz NOT NULL DEFAULT now(),
  active boolean NOT NULL DEFAULT true
);
-- statement-breakpoint
CREATE UNIQUE INDEX tester_builds_one_active_platform_idx
  ON tester_builds (platform) WHERE active;
-- statement-breakpoint
CREATE INDEX tester_builds_active_created_idx
  ON tester_builds (platform, created_at DESC) WHERE active;
-- statement-breakpoint
CREATE FUNCTION public.set_active_tester_build(
  p_version text, p_platform text, p_download_url text, p_file_name text, p_sha256 text, p_release_notes text
) RETURNS public.tester_builds
LANGUAGE plpgsql VOLATILE SECURITY INVOKER
SET search_path = pg_catalog, public AS $$
DECLARE result public.tester_builds%ROWTYPE;
BEGIN
  PERFORM pg_advisory_xact_lock(hashtext('icepunk-tester-build:' || p_platform));
  UPDATE public.tester_builds SET active = false WHERE platform = p_platform AND active;
  INSERT INTO public.tester_builds(version, platform, download_url, file_name, sha256, release_notes, active)
  VALUES (p_version, p_platform, p_download_url, p_file_name, p_sha256, p_release_notes, true)
  RETURNING * INTO result;
  RETURN result;
END;
$$;