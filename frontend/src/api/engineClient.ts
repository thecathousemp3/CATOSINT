export type DetectedModel = {
  provider: string
  model_id: string
  completion_endpoint: string
}

export type LocalEngineSnapshot = {
  status: 'connected'
  models: DetectedModel[]
  diagnostics: string[]
}

export type SourceConfiguration = {
  name: string
  url: string
  records_path: string
  content_path: string
  interval_seconds: number
  status: string
  authentication_configured?: boolean
}

export type AnalysisResult = {
  records_analyzed: number
  analysis: string
  observations: Array<{
    source_id: string
    title: string
    content: string
    source_url: string
    observed_at_epoch_seconds: number
  }>
  sources: Array<{
    source: string
    records_received: number
    skipped_by_rate_limit: boolean
    skipped_by_batch_limit: boolean
    error: string
  }>
}

const engineBase = 'http://127.0.0.1:8765'

async function getJson<T>(path: string, timeoutMs = 8000): Promise<T> {
  const response = await fetch(`${engineBase}${path}`, {
    headers: { Accept: 'application/json' },
    signal: AbortSignal.timeout(timeoutMs),
  })
  if (!response.ok) {
    throw new Error(`Local engine returned HTTP ${response.status}`)
  }
  return response.json() as Promise<T>
}

async function postJson<T>(path: string, value: unknown): Promise<T> {
  const response = await fetch(`${engineBase}${path}`, {
    method: 'POST',
    headers: {
      Accept: 'application/json',
      'Content-Type': 'application/json',
    },
    body: JSON.stringify(value),
    signal: AbortSignal.timeout(480_000),
  })
  const result = await response.json() as { error?: string } & T
  if (!response.ok) {
    throw new Error(result.error ?? `Local engine returned HTTP ${response.status}`)
  }
  return result
}

export async function inspectLocalEngine(serverUrl: string): Promise<LocalEngineSnapshot> {
  const health = await getJson<{ status: string }>('/api/v1/health')
  if (health.status !== 'ok') {
    throw new Error('Local engine health response was not recognized')
  }

  const query = serverUrl ? `?${new URLSearchParams({ server_url: serverUrl })}` : ''
  const discovery = await getJson<{
    models: DetectedModel[]
    diagnostics: string[]
  }>(`/api/v1/models${query}`, 30000)

  if (!Array.isArray(discovery.models) || !Array.isArray(discovery.diagnostics)) {
    throw new Error('Local engine model discovery response was malformed')
  }

  return {
    status: 'connected',
    models: discovery.models,
    diagnostics: discovery.diagnostics,
  }
}

export async function listSources(): Promise<SourceConfiguration[]> {
  const result = await getJson<{ sources: SourceConfiguration[] }>('/api/v1/sources')
  if (!Array.isArray(result.sources)) {
    throw new Error('Local engine source-list response was malformed')
  }
  return result.sources
}

export function createSource(source: {
  name: string
  url: string
  records_path: string
  content_path: string
  auth_header?: string
  auth_prefix?: string
  api_key?: string
}): Promise<SourceConfiguration> {
  return postJson<SourceConfiguration>('/api/v1/sources', source)
}

export function runAnalysis(modelId: string, serverUrl: string): Promise<AnalysisResult> {
  return postJson<AnalysisResult>('/api/v1/analyze', {
    model_id: modelId,
    server_url: serverUrl,
  })
}
