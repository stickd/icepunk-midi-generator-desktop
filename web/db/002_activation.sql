ALTER TABLE devices RENAME COLUMN fingerprint TO device_id;
-- statement-breakpoint
ALTER TABLE devices ADD COLUMN last_seen_at timestamptz NOT NULL DEFAULT now();
-- statement-breakpoint
CREATE FUNCTION public.activate_license(p_key_hash text, p_device_id text)
RETURNS jsonb LANGUAGE plpgsql VOLATILE SECURITY INVOKER
SET search_path = pg_catalog, public AS $$
DECLARE
  lic public.licenses%ROWTYPE;
  registration uuid;
  total integer;
BEGIN
  IF current_setting('transaction_isolation') <> 'read committed' THEN
    RAISE EXCEPTION 'Activation requires READ COMMITTED isolation';
  END IF;
  -- Separate statements inside this VOLATILE function obtain fresh snapshots
  -- under READ COMMITTED after waiting for the license-row lock.
  SELECT * INTO lic FROM public.licenses WHERE key_hash = p_key_hash FOR UPDATE;
  IF NOT FOUND THEN RETURN jsonb_build_object('code', 'INVALID_LICENSE'); END IF;
  IF lic.status <> 'active' THEN RETURN jsonb_build_object('code', 'LICENSE_REVOKED'); END IF;
  SELECT id INTO registration FROM public.devices
    WHERE license_id = lic.id AND device_id = p_device_id;
  IF registration IS NULL THEN
    SELECT count(*) INTO total FROM public.devices WHERE license_id = lic.id;
    IF total >= lic.max_devices THEN RETURN jsonb_build_object('code', 'DEVICE_LIMIT_REACHED'); END IF;
    INSERT INTO public.devices (license_id, device_id)
      VALUES (lic.id, p_device_id) RETURNING id INTO registration;
  ELSE
    UPDATE public.devices SET last_seen_at = clock_timestamp() WHERE id = registration;
  END IF;
  RETURN jsonb_build_object('ok', true, 'licenseId', lic.id,
    'instagram', '@' || lic.instagram_username, 'licenseType', lic.license_type,
    'maxDevices', lic.max_devices);
END;
$$;
-- statement-breakpoint
CREATE FUNCTION public.manage_license(p_license_id uuid, p_action text)
RETURNS boolean LANGUAGE plpgsql VOLATILE SECURITY INVOKER
SET search_path = pg_catalog, public AS $$
BEGIN
  IF p_action NOT IN ('reset', 'revoke', 'reactivate') OR p_action IS NULL THEN
    RAISE EXCEPTION 'Invalid action';
  END IF;
  PERFORM id FROM public.licenses WHERE id = p_license_id FOR UPDATE;
  IF NOT FOUND THEN RETURN false; END IF;
  IF p_action = 'reset' THEN
    DELETE FROM public.devices WHERE license_id = p_license_id;
  ELSE
    UPDATE public.licenses SET status = CASE WHEN p_action = 'revoke' THEN 'revoked' ELSE 'active' END
      WHERE id = p_license_id;
  END IF;
  RETURN true;
END;
$$;

