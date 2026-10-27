import { useEffect, useMemo, useState, type FormEvent } from 'react'
import {
  createSource,
  inspectLocalEngine,
  listSources,
  runAnalysis,
  type AnalysisResult,
  type DetectedModel,
  type LocalEngineSnapshot,
  type SourceConfiguration,
} from './api/engineClient'
import './App.css'

type IconName =
  | 'grid'
  | 'radio'
  | 'layers'
  | 'file'
  | 'settings'
  | 'search'
  | 'plus'
  | 'arrow'
  | 'clock'
  | 'globe'
  | 'spark'
  | 'close'
  | 'external'
  | 'shield'

type Observation = {
  id: string
  title: string
  source: string
  time: string
  content?: string
  sourceUrl: string
}

type ConfiguredSource = {
  id: string
  name: string
  endpoint: string
  contentPath: string
  recordsPath: string
}

function Icon({
  name,
  size = 18,
}: {
  name: IconName
  size?: number
}) {
  const paths: Record<IconName, string> = {
    grid: 'M3 3h7v7H3z M14 3h7v7h-7z M14 14h7v7h-7z M3 14h7v7H3z',
    radio: 'M12 12a2 2 0 1 0 0 .01 M5.6 5.6a9 9 0 0 0 0 12.8 M18.4 5.6a9 9 0 0 1 0 12.8 M2.8 2.8a13 13 0 0 0 0 18.4 M21.2 2.8a13 13 0 0 1 0 18.4',
    layers: 'm12 3 9 5-9 5-9-5 9-5z M3 12l9 5 9-5 M3 16l9 5 9-5',
    file: 'M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8z M14 2v6h6 M8 13h8 M8 17h8',
    settings: 'M12 8a4 4 0 1 0 0 8 4 4 0 0 0 0-8z M19.4 15a1.7 1.7 0 0 0 .3 1.9l.1.1-1.6 2.8-.2-.1a1.7 1.7 0 0 0-1.8.5l-.1.2h-3.2l-.1-.2a1.7 1.7 0 0 0-1.8-.5l-.2.1-1.6-2.8.1-.1a1.7 1.7 0 0 0 .3-1.9l-.1-.2-2.9-.9v-3.2l.2-.1a1.7 1.7 0 0 0 1.1-1.5l-.1-.2 1.6-2.8.2.1a1.7 1.7 0 0 0 1.8-.5l.1-.2h3.2l.1.2a1.7 1.7 0 0 0 1.8.5l.2-.1 1.6 2.8-.1.2a1.7 1.7 0 0 0 .3 1.9l.2.1v3.2l-2.9.9z',
    search: 'm20 20-4.4-4.4 M18 10.5a7.5 7.5 0 1 1-15 0 7.5 7.5 0 0 1 15 0z',
    plus: 'M12 5v14 M5 12h14',
    arrow: 'M5 12h14 M13 6l6 6-6 6',
    clock: 'M12 8v4l3 2 M22 12a10 10 0 1 1-20 0 10 10 0 0 1 20 0z',
    globe: 'M12 22a10 10 0 1 0 0-20 10 10 0 0 0 0 20z M2 12h20 M12 2a15 15 0 0 1 0 20 M12 2a15 15 0 0 0 0 20',
    spark: 'm12 3 1.9 5.8L20 11l-6.1 2.2L12 19l-1.9-5.8L4 11l6.1-2.2L12 3z M19 14l1.2 2.8L23 18l-2.8 1.2L19 22l-1.2-2.8L15 18l2.8-1.2L19 14z',
    close: 'M18 6 6 18 M6 6l12 12',
    external: 'M14 3h7v7 M10 14 21 3 M18 13v6a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V8a2 2 0 0 1 2-2h6',
    shield: 'M12 22s8-4 8-11V5l-8-3-8 3v6c0 7 8 11 8 11z M9 12l2 2 4-4',
  }

  return (
    <svg
      aria-hidden="true"
      className="icon"
      width={size}
      height={size}
      viewBox="0 0 24 24"
      fill="none"
      stroke="currentColor"
      strokeWidth="1.7"
      strokeLinecap="round"
      strokeLinejoin="round"
    >
      {paths[name].split(' M').map((path, index) => (
        <path key={`${name}-${index}`} d={`${index ? 'M' : ''}${path}`} />
      ))}
    </svg>
  )
}

function safeSourceUrl(value: string) {
  try {
    const url = new URL(value)
    return url.protocol === 'https:' ? url.toString() : ''
  } catch {
    return ''
  }
}

function App() {
  const [activeNav, setActiveNav] = useState('Overview')
  const [search, setSearch] = useState('')
  const [configuredSources, setConfiguredSources] = useState<ConfiguredSource[]>([])
  const [remoteSources, setRemoteSources] = useState<SourceConfiguration[]>([])
  const [sourceDialogOpen, setSourceDialogOpen] = useState(false)
  const [engineDialogOpen, setEngineDialogOpen] = useState(false)
  const [formError, setFormError] = useState('')
  const [sourceName, setSourceName] = useState('')
  const [sourceEndpoint, setSourceEndpoint] = useState('')
  const [recordsPath, setRecordsPath] = useState('')
  const [contentPath, setContentPath] = useState('body')
  const [apiKeyHeader, setApiKeyHeader] = useState('Authorization')
  const [apiKeyPrefix, setApiKeyPrefix] = useState('Bearer ')
  const [apiKey, setApiKey] = useState('')
  const [notice, setNotice] = useState('')
  const [engine, setEngine] = useState<LocalEngineSnapshot | null>(null)
  const [engineOnline, setEngineOnline] = useState(false)
  const [engineError, setEngineError] = useState('')
  const [isCheckingEngine, setIsCheckingEngine] = useState(false)
  const [refreshRevision, setRefreshRevision] = useState(0)
  const [selectedModel, setSelectedModel] = useState('')
  const [modelServerUrl, setModelServerUrl] = useState(
    () => window.localStorage.getItem('catosint.model-server-url') ?? '',
  )
  const [modelServerDraft, setModelServerDraft] = useState(
    () => window.localStorage.getItem('catosint.model-server-url') ?? '',
  )
  const [modelServerError, setModelServerError] = useState('')
  const [analysisResult, setAnalysisResult] = useState<AnalysisResult | null>(null)
  const [analysisError, setAnalysisError] = useState('')
  const [isAnalyzing, setIsAnalyzing] = useState(false)

  useEffect(() => {
    let active = true
    let checking = false
    const check = async () => {
      if (checking) return
      checking = true
      if (active) setIsCheckingEngine(true)
      try {
        const snapshot = await inspectLocalEngine(modelServerUrl)
        if (active) {
          setEngine(snapshot)
          setEngineOnline(true)
          setEngineError('')
          setSelectedModel((current) =>
            current && snapshot.models.some((model) => model.model_id === current)
              ? current
              : snapshot.models[0]?.model_id ?? '',
          )
          void listSources()
            .then((sources) => {
              if (active) setRemoteSources(sources)
            })
            .catch((error: unknown) => {
              if (active) {
                setNotice(error instanceof Error ? error.message : 'Could not load configured sources.')
              }
            })
        }
      } catch (error) {
        if (active) {
          setEngineOnline(false)
          setEngine(null)
          setSelectedModel('')
          setRemoteSources([])
          setEngineError(error instanceof Error ? error.message : 'Local engine is unavailable')
        }
      } finally {
        checking = false
        if (active) setIsCheckingEngine(false)
      }
    }

    void check()
    const intervalId = window.setInterval(() => void check(), 15000)
    return () => {
      active = false
      window.clearInterval(intervalId)
    }
  }, [modelServerUrl, refreshRevision])

  useEffect(() => {
    if (!sourceDialogOpen && !engineDialogOpen) return
    const handleKeyDown = (event: KeyboardEvent) => {
      if (event.key === 'Escape') {
        closeSourceDialog()
        setEngineDialogOpen(false)
      }
    }
    window.addEventListener('keydown', handleKeyDown)
    return () => window.removeEventListener('keydown', handleKeyDown)
  }, [sourceDialogOpen, engineDialogOpen])

  const filteredObservations = useMemo(() => {
    const query = search.trim().toLowerCase()
    const observations = (analysisResult?.observations ?? []).map((observation, index): Observation => ({
      id: `${observation.source_id}-${index}`,
      title: observation.title || observation.content.slice(0, 100),
      source: observation.source_id.split(':')[0],
      time: new Date(observation.observed_at_epoch_seconds * 1000).toLocaleTimeString(),
      content: observation.content,
      sourceUrl: safeSourceUrl(observation.source_url),
    }))
    return observations.filter((observation) => {
      const matchesQuery =
        !query ||
        `${observation.title} ${observation.source} ${observation.content ?? ''}`
          .toLowerCase()
          .includes(query)
      return matchesQuery
    })
  }, [analysisResult, search])
  const hasAnalysis = analysisResult !== null

  async function addSource(event: FormEvent<HTMLFormElement>) {
    event.preventDefault()
    setFormError('')

    let parsedEndpoint: URL
    try {
      parsedEndpoint = new URL(sourceEndpoint)
    } catch {
      setFormError('Enter a valid HTTPS API endpoint.')
      return
    }

    if (parsedEndpoint.protocol !== 'https:') {
      setFormError('Source endpoints must use HTTPS.')
      return
    }
    if (!sourceName.trim() || !contentPath.trim()) {
      setFormError('Name and record content path are required.')
      return
    }
    if (apiKey.trim() && !engineOnline) {
      setFormError('Start the local engine before saving API credentials. Keys are not stored in the browser.')
      return
    }

    if (engineOnline) {
      try {
        const registered = await createSource({
          name: sourceName.trim(),
          url: parsedEndpoint.toString(),
          records_path: recordsPath.trim(),
          content_path: contentPath.trim(),
          ...(apiKey.trim()
            ? {
                auth_header: apiKeyHeader.trim() || 'Authorization',
                auth_prefix: apiKeyPrefix,
                api_key: apiKey,
              }
            : {}),
        })
        setRemoteSources((sources) => [
          ...sources.filter((source) => source.name !== registered.name),
          registered,
        ])
        setNotice(`"${registered.name}" registered with the local C++ engine.`)
      } catch (error) {
        setFormError(error instanceof Error ? error.message : 'Could not register the source.')
        return
      }
    } else {
      setConfiguredSources((sources) => [
        ...sources,
        {
          id: crypto.randomUUID(),
          name: sourceName.trim(),
          endpoint: parsedEndpoint.toString(),
          contentPath: contentPath.trim(),
          recordsPath: recordsPath.trim(),
        },
      ])
      setNotice('Source saved as a local draft. Start the C++ service to register it.')
    }

    setSourceName('')
    setSourceEndpoint('')
    setRecordsPath('')
    setContentPath('body')
    setApiKey('')
    setSourceDialogOpen(false)
  }

  function saveModelServer(event: FormEvent<HTMLFormElement>) {
    event.preventDefault()
    setModelServerError('')
    const value = modelServerDraft.trim()
    if (value) {
      let parsed: URL
      try {
        parsed = new URL(value)
      } catch {
        setModelServerError('Enter a valid local server base URL.')
        return
      }
      const localHosts = ['localhost', '127.0.0.1', '[::1]']
      if (
        !['http:', 'https:'].includes(parsed.protocol) ||
        !localHosts.includes(parsed.hostname) ||
        parsed.pathname !== '/' ||
        parsed.search ||
        parsed.hash
      ) {
        setModelServerError('Use a loopback URL only, such as http://127.0.0.1:1234.')
        return
      }
      window.localStorage.setItem('catosint.model-server-url', parsed.origin)
      setModelServerDraft(parsed.origin)
      setModelServerUrl(parsed.origin)
    } else {
      window.localStorage.removeItem('catosint.model-server-url')
      setModelServerUrl('')
    }
    setRefreshRevision((revision) => revision + 1)
  }

  async function analyzeSources() {
    if (!selectedModel || remoteSources.length === 0 || isAnalyzing) return
    setIsAnalyzing(true)
    setAnalysisError('')
    try {
      const result = await runAnalysis(selectedModel, modelServerUrl)
      setAnalysisResult(result)
      const sourceErrors = result.sources
        .filter((source) => source.error)
        .map((source) => `${source.source}: ${source.error}`)
      setAnalysisError(sourceErrors.join(' · '))
      setNotice(result.records_analyzed
        ? `Analyzed ${result.records_analyzed} record(s) with ${selectedModel}.`
        : 'No new records were returned. Review source status and polling intervals.')
    } catch (error) {
      setAnalysisError(error instanceof Error ? error.message : 'Analysis request failed.')
    } finally {
      setIsAnalyzing(false)
    }
  }

  function closeSourceDialog() {
    setSourceDialogOpen(false)
    setFormError('')
    setApiKey('')
  }

  return (
    <div className="app-shell">
      <aside className="sidebar">
        <a className="brand" href="#" aria-label="CAT OSINT home">
          <span className="brand-mark" aria-hidden="true">
            <svg viewBox="0 0 64 64" role="img" shapeRendering="crispEdges">
              <path d="M7 29 10 7l14 10h16L54 7l3 22c1 15-9 27-25 30C16 56 6 44 7 29Z" fill="#f2f2f2" />
              <path d="m14 18 1-7 8 6Zm36 0-1-7-8 6Z" fill="#080808" />
              <path d="M13 25h15v12H13zM36 25h15v12H36z" fill="#080808" />
              <path d="M16 28h9v6h-9zM39 28h9v6h-9z" fill="#f2f2f2" />
              <path d="M19 29h3v4h-3zM42 29h3v4h-3zM28 29h8v4h-8z" fill="#080808" />
              <path d="m32 38-4 4h8zM32 42v4m0 0-5 2m5-2 5 2M8 38l13 2m-14 3 12 1m37-6-13 2m14 3-12 1" fill="none" stroke="#080808" strokeWidth="2" strokeLinecap="square" />
            </svg>
          </span>
          <span className="brand-copy">
            <strong>CAT OSINT</strong>
            <small>PUBLIC INFORMATION MONITOR</small>
          </span>
        </a>

        <div className="workspace-switcher">
          <span className="workspace-avatar">P</span>
          <span>
            <strong>Personal workspace</strong>
            <small>Local engine</small>
          </span>
          <span className="switcher-caret">⌄</span>
        </div>

        <nav className="primary-nav" aria-label="Main navigation">
          <p className="nav-heading">WORKSPACE</p>
          {[
            ['Overview', 'grid'],
            ['Sources', 'radio'],
            ['Collections', 'layers'],
            ['Reports', 'file'],
          ].map(([label, icon]) => (
            <button
              aria-label={label}
              className={`nav-link ${activeNav === label ? 'active' : ''}`}
              key={label}
              onClick={() => {
                setActiveNav(label)
                if (label !== 'Overview') {
                  setNotice(`${label} view is part of the upcoming engine-connected UI.`)
                } else {
                  setNotice('')
                }
              }}
              type="button"
            >
              <Icon name={icon as IconName} />
              <span>{label}</span>
              {label === 'Sources' && remoteSources.length + configuredSources.length > 0 && (
                <span className="nav-count">{remoteSources.length + configuredSources.length}</span>
              )}
            </button>
          ))}
        </nav>

        <div className="sidebar-spacer" />

        <div className="privacy-card">
          <div className="privacy-icon"><Icon name="shield" size={17} /></div>
          <div>
            <strong>Local data control</strong>
            <p>Sources are collected only when you explicitly run an analysis.</p>
          </div>
        </div>

        <button className="nav-link settings-link" type="button" onClick={() => setEngineDialogOpen(true)}>
          <Icon name="settings" />
          <span>Settings</span>
        </button>
        <div className="sidebar-user">
          <span className="user-avatar">C</span>
          <span><strong>CAT OSINT</strong><small>Local instance</small></span>
          <span className="more-dots" aria-hidden="true">···</span>
        </div>
      </aside>

      <main className="main-content">
        <header className="topbar">
          <div className="breadcrumbs">
            <span>Workspace</span>
            <span className="breadcrumb-divider">/</span>
            <strong>{activeNav}</strong>
          </div>
          <div className="topbar-actions">
            <span className="analysis-status-chip">
              <span className={`analysis-status-dot ${hasAnalysis ? 'live' : ''}`} />
              {hasAnalysis ? 'ANALYSIS COMPLETE' : 'NO ANALYSIS'}
            </span>
            <button className="engine-status" type="button" onClick={() => setEngineDialogOpen(true)}>
              <span className={`status-dot ${engineOnline ? 'online' : 'offline'}`} />
              {engineOnline ? 'Engine connected' : 'Engine offline'}
              <span className="status-chevron">⌄</span>
            </button>
            <button
              aria-label="Open local engine connection details"
              className="avatar-button"
              onClick={() => setEngineDialogOpen(true)}
              type="button"
            >
              C
            </button>
          </div>
        </header>

        <div className="page-wrap">
          <section className="page-heading">
            <div>
              <div className="eyebrow"><span className="eyebrow-line" /> PUBLIC INFORMATION WORKSPACE</div>
              <h1>Operations overview<span className="heading-period">.</span></h1>
              <p className="page-subtitle">Configured sources, collection status, and recent observations.</p>
            </div>
            <div className="heading-actions">
              {engineOnline && selectedModel && remoteSources.length > 0 ? (
                <button
                  className="button button-secondary"
                  disabled={isAnalyzing}
                  onClick={() => void analyzeSources()}
                  type="button"
                >
                  <Icon name={isAnalyzing ? 'clock' : 'spark'} size={16} />
                  {isAnalyzing ? 'Analyzing…' : 'Run analysis'}
                </button>
              ) : (
                <button className="button button-secondary" type="button" onClick={() => setEngineDialogOpen(true)}>
                  <Icon name="globe" size={16} />
                  {engineOnline ? 'Model settings' : 'Connect engine'}
                </button>
              )}
              <button className="button button-primary" type="button" onClick={() => setSourceDialogOpen(true)}>
                <Icon name="plus" size={16} />
                Add source
              </button>
            </div>
          </section>

          {notice && (
            <div className="notice" role="status">
              <span>{notice}</span>
              <button type="button" aria-label="Dismiss notice" onClick={() => setNotice('')}>
                <Icon name="close" size={15} />
              </button>
            </div>
          )}
          {analysisError && (
            <div className="analysis-error" role="alert">
              <span>{analysisError}</span>
              <button aria-label="Dismiss analysis error" onClick={() => setAnalysisError('')} type="button">
                <Icon name="close" size={15} />
              </button>
            </div>
          )}

          <section className="metrics-grid" aria-label="System status">
            <article className="metric-card">
              <div className="metric-top">
                <span className="metric-label">CONFIGURED SOURCES</span>
                <span className="metric-icon teal"><Icon name="radio" size={17} /></span>
              </div>
              <div className="metric-value">{remoteSources.length + configuredSources.length}<span className="metric-unit"> sources</span></div>
              <div className="metric-foot"><span className="metric-note">{engineOnline ? 'Stored by local engine' : 'Local drafts only'}</span></div>
            </article>
            <article className="metric-card">
              <div className="metric-top">
                <span className="metric-label">OBSERVATIONS</span>
                <span className="metric-icon purple"><Icon name="layers" size={17} /></span>
              </div>
              <div className="metric-value">{analysisResult?.records_analyzed ?? 0}<span className="metric-unit"> records processed</span></div>
              <div className="metric-foot"><span className="metric-note">{hasAnalysis ? 'Latest collection result' : 'No collection run yet'}</span></div>
            </article>
            <article className="metric-card model-metric">
              <div className="metric-top">
                <span className="metric-label">LOCAL MODEL</span>
                <span className="metric-icon amber"><Icon name="spark" size={17} /></span>
              </div>
              <div className="metric-value model-state">
                {selectedModel || (engineOnline ? 'No model detected' : 'Not connected')}
              </div>
              <div className="metric-foot">
                <span className={`status-dot ${selectedModel ? 'online' : 'offline'}`} />
                <span className="metric-note">
                  {selectedModel
                    ? engine?.models.find((model) => model.model_id === selectedModel)?.provider
                    : engineOnline ? 'Enable its local API server' : 'Waiting for local engine'}
                </span>
              </div>
            </article>
          </section>
          {engineOnline && engine?.models.length ? (
            <section aria-label="Detected local model" className="detected-model-bar">
              <span className="detected-model-icon"><Icon name="spark" size={15} /></span>
              <span className="detected-model-copy">
                <strong>Local model detected</strong>
                <small>{engine.models.length} model{engine.models.length === 1 ? '' : 's'} found on this machine</small>
              </span>
              <label className="model-select-label">
                  <span className="sr-only">Choose local model</span>
                <select value={selectedModel} onChange={(event) => setSelectedModel(event.target.value)}>
                  {engine.models.map((model: DetectedModel) => (
                    <option key={`${model.provider}:${model.model_id}`} value={model.model_id}>
                      {model.provider} · {model.model_id}
                    </option>
                  ))}
                </select>
              </label>
            </section>
          ) : null}

          <section className="content-grid">
            <div className="left-column">
              <article className="panel activity-panel">
                <div className="panel-heading">
                  <div>
                    <div className="panel-kicker">COLLECTION STATUS</div>
                    <h2>Latest collection</h2>
                  </div>
                </div>
                {analysisResult ? (
                  <>
                    <div className="activity-summary">
                      <strong>{analysisResult.records_analyzed}</strong>
                      <span>records processed in the latest run</span>
                    </div>
                    <div className="source-run-list">
                      {analysisResult.sources.map((source) => (
                        <div className="source-run-row" key={source.source}>
                          <span>{source.source}</span>
                          <span className={source.error ? 'source-run-error' : ''}>
                            {source.error || `${source.records_received} records received`}
                          </span>
                        </div>
                      ))}
                    </div>
                  </>
                ) : (
                  <div className="collection-empty">
                    <strong>No collection has run.</strong>
                    <span>
                      {!engineOnline
                        ? 'Start the local engine to load configured sources.'
                        : !selectedModel
                          ? 'Start a supported local model API server to enable analysis.'
                          : remoteSources.length === 0
                            ? 'Configure a public API source to begin.'
                            : 'Run analysis to fetch current records from configured sources.'}
                    </span>
                  </div>
                )}
              </article>

              <article className="panel observations-panel">
                <div className="panel-heading observations-heading">
                  <div>
                    <div className="panel-kicker">COLLECTED RECORDS</div>
                    <h2>Latest observations</h2>
                  </div>
                </div>
                <div className="feed-controls">
                  <label className="search-field">
                    <Icon name="search" size={15} />
                    <span className="sr-only">Search observations</span>
                    <input
                      onChange={(event) => setSearch(event.target.value)}
                      placeholder="Search feed"
                      type="search"
                      value={search}
                    />
                  </label>
                </div>
                <div className="observation-list">
                  {filteredObservations.length ? filteredObservations.map((observation) => (
                    <article className="observation-row" key={observation.id}>
                      <span className="observation-marker" />
                      <div className="observation-main">
                        <div className="observation-meta">
                          <span className="category-tag">SOURCE</span>
                        </div>
                        <h3>{observation.title}</h3>
                        <div className="observation-source">{observation.source}</div>
                        {observation.content && <p className="observation-content">{observation.content}</p>}
                        {observation.sourceUrl && (
                          <a className="observation-link" href={observation.sourceUrl} rel="noreferrer" target="_blank">
                            View original source <Icon name="external" size={12} />
                          </a>
                        )}
                      </div>
                      <time className="observation-time"><Icon name="clock" size={13} /> {observation.time}</time>
                    </article>
                  )) : (
                    <div className="empty-feed">
                      {search.trim()
                        ? 'No collected records match this search.'
                        : hasAnalysis
                          ? 'The latest run returned no records.'
                          : 'No records have been collected.'}
                    </div>
                  )}
                </div>
                {hasAnalysis && filteredObservations.length > 0 && (
                  <div className="feed-footer">
                    <span>Records fetched by the local engine</span>
                    <span className="feed-footer-right">Original source links included <Icon name="shield" size={13} /></span>
                  </div>
                )}
              </article>
            </div>

            <aside className="right-column">
              <article className="panel brief-panel">
                <div className="brief-heading">
                  <div className="brief-icon"><Icon name="spark" size={17} /></div>
                  <div>
                    <div className="panel-kicker">ANALYSIS SUMMARY</div>
                    <span className="brief-status">{hasAnalysis ? 'Local model output' : 'Awaiting analysis'}</span>
                  </div>
                </div>
                <h2>{hasAnalysis ? 'Latest analysis' : 'No analysis yet'}</h2>
                <div className="brief-divider" />
                {hasAnalysis ? (
                  <>
                    <p className="brief-intro">
                      {analysisResult?.records_analyzed ?? 0} records processed. Verify claims against their original sources.
                    </p>
                    <p className="analysis-output">
                      {analysisResult?.analysis || 'The model returned no analysis text.'}
                    </p>
                  </>
                ) : (
                  <p className="brief-intro">
                    A local model and configured data sources are required to produce an analysis.
                  </p>
                )}
                {analysisResult?.observations.length ? (
                  <div className="citation-box">
                    <div className="citation-top">
                      <span>SOURCE RECORDS</span>
                      <span>{analysisResult.observations.length} RECORDS</span>
                    </div>
                    {analysisResult.observations.slice(0, 4).map((observation, index) => (
                    <div className="citation-row" key={`${observation.source_id}-${index}`}>
                      <span className="citation-id">{String(index + 1).padStart(2, '0')}</span>
                      <span>{observation.title || observation.source_id}</span>
                      <Icon name="shield" size={13} />
                    </div>
                    ))}
                  </div>
                ) : null}
                {hasAnalysis && (
                  <div className="brief-disclaimer"><Icon name="shield" size={14} /> Model output is advisory; verify original sources.</div>
                )}
              </article>

              <article className="panel sources-panel">
                <div className="panel-heading sources-heading">
                  <div>
                    <div className="panel-kicker">SOURCE WORKSPACE</div>
                    <h2>Configured sources</h2>
                  </div>
                  <button className="icon-button" aria-label="Add source" onClick={() => setSourceDialogOpen(true)} type="button">
                    <Icon name="plus" size={16} />
                  </button>
                </div>
                <div className="source-list">
                  {remoteSources.map((source) => {
                    const latestStatus = analysisResult?.sources.find(
                      (status) => status.source === source.name,
                    )
                    const statusLabel = latestStatus
                      ? latestStatus.error
                        ? 'ERROR'
                        : latestStatus.skipped_by_rate_limit
                          ? 'RATE LIMITED'
                          : `${latestStatus.records_received} RECEIVED`
                      : 'CONFIGURED'
                    return (
                      <div className="source-item" key={`engine-${source.name}`}>
                        <span className="source-symbol draft"><Icon name="globe" size={15} /></span>
                        <span className="source-copy"><strong>{source.name}</strong><small>{source.url}</small></span>
                        <span className={`source-status ${latestStatus?.error ? 'source-error' : 'ready-status'}`}>{statusLabel}</span>
                      </div>
                    )
                  })}
                  {configuredSources.map((source) => (
                    <div className="source-item" key={source.id}>
                      <span className="source-symbol draft"><Icon name="globe" size={15} /></span>
                      <span className="source-copy"><strong>{source.name}</strong><small>{source.endpoint}</small></span>
                      <span className="source-status draft-status">DRAFT</span>
                    </div>
                  ))}
                  {remoteSources.length === 0 && configuredSources.length === 0 && (
                    <div className="empty-feed">No sources configured.</div>
                  )}
                </div>
                <button className="add-source-link" type="button" onClick={() => setSourceDialogOpen(true)}>
                  <Icon name="plus" size={14} /> Configure another source
                </button>
              </article>
            </aside>
          </section>

          <footer className="page-footer">
            <span>CAT OSINT <span className="footer-version">Local instance</span></span>
            <span><span className={`footer-dot ${engineOnline ? 'online' : ''}`} /> Local-first · Collection runs on request</span>
            <button type="button" onClick={() => setEngineDialogOpen(true)}>System status <Icon name="arrow" size={13} /></button>
          </footer>
        </div>
      </main>

      {sourceDialogOpen && (
        <div className="modal-backdrop" onMouseDown={(event) => {
          if (event.target === event.currentTarget) closeSourceDialog()
        }}>
          <section aria-labelledby="source-modal-title" aria-modal="true" className="modal-card" role="dialog">
            <div className="modal-heading">
              <div className="modal-icon"><Icon name="radio" size={19} /></div>
              <button aria-label="Close dialog" className="icon-button" onClick={closeSourceDialog} type="button"><Icon name="close" size={17} /></button>
            </div>
            <p className="panel-kicker">LOCAL SOURCE CONFIGURATION</p>
            <h2 id="source-modal-title">Add a public API source</h2>
            <p className="modal-copy">
              {engineOnline
                ? 'Register a documented HTTPS JSON API. It is fetched only when you explicitly run an analysis.'
                : 'Draft a public HTTPS JSON API mapping. Start the engine to register it or add credentials.'}
            </p>
            <form className="source-form" onSubmit={addSource}>
              <label>
                <span>Source name</span>
                <input autoFocus onChange={(event) => setSourceName(event.target.value)} placeholder="e.g. Public weather feed" value={sourceName} />
              </label>
              <label>
                <span>HTTPS endpoint</span>
                <input onChange={(event) => setSourceEndpoint(event.target.value)} placeholder="https://api.example.org/v1/updates" type="url" value={sourceEndpoint} />
              </label>
              <label>
                <span>Records array path</span>
                <input onChange={(event) => setRecordsPath(event.target.value)} placeholder="data.items (leave blank for root array)" value={recordsPath} />
                <small>Dot-separated path to the response array.</small>
              </label>
              <label>
                <span>Record content field</span>
                <input onChange={(event) => setContentPath(event.target.value)} placeholder="description" value={contentPath} />
                <small>Dot-separated path inside each record.</small>
              </label>
              {formError && <p className="form-error" role="alert">{formError}</p>}
              <label>
                <span>API key header</span>
                <input
                  autoComplete="off"
                  onChange={(event) => setApiKeyHeader(event.target.value)}
                  placeholder="Authorization or X-API-Key"
                  value={apiKeyHeader}
                />
              </label>
              <label>
                <span>Header prefix</span>
                <input
                  autoComplete="off"
                  onChange={(event) => setApiKeyPrefix(event.target.value)}
                  placeholder="Bearer "
                  value={apiKeyPrefix}
                />
                <small>Leave blank for providers that expect the key without a prefix.</small>
              </label>
              <label>
                <span>API key</span>
                <input
                  autoComplete="new-password"
                  onChange={(event) => setApiKey(event.target.value)}
                  type="password"
                  value={apiKey}
                />
                <small>Kept in the running engine’s memory; not saved in browser storage or returned by the API.</small>
              </label>
              <div className="form-note"><Icon name="shield" size={14} /> API keys are held only in the local engine’s memory.</div>
              <div className="modal-actions">
                <button className="button button-secondary" onClick={closeSourceDialog} type="button">Cancel</button>
                <button className="button button-primary" disabled={isAnalyzing} type="submit">
                  {engineOnline ? 'Register source' : 'Save draft source'} <Icon name="arrow" size={15} />
                </button>
              </div>
            </form>
          </section>
        </div>
      )}

      {engineDialogOpen && (
        <div className="modal-backdrop" onMouseDown={(event) => {
          if (event.target === event.currentTarget) setEngineDialogOpen(false)
        }}>
          <section aria-labelledby="engine-modal-title" aria-modal="true" className="modal-card engine-modal" role="dialog">
            <div className="modal-heading">
              <div className="modal-icon"><Icon name="globe" size={19} /></div>
              <button aria-label="Close dialog" className="icon-button" onClick={() => setEngineDialogOpen(false)} type="button"><Icon name="close" size={17} /></button>
            </div>
            <p className="panel-kicker">NATIVE ENGINE</p>
            <h2 id="engine-modal-title">{engineOnline ? 'Local engine settings' : 'Connect the local engine'}</h2>
            <p className="modal-copy">
              {engineOnline
                ? 'The C++ service is responding. Model discovery checks every 15 seconds, including Bionic on port 1235, and probes only standard local providers plus the configured loopback address. No process or network scanning is performed.'
                : 'Start the C++ service from a Developer PowerShell, then check again. The browser only contacts the loopback API at 127.0.0.1:8765.'}
            </p>
            <div className="connection-detail">
              <span className={`status-dot ${engineOnline ? 'online' : 'offline'}`} />
              <span><strong>Engine status</strong><small>{engineOnline ? 'Connected · source settings remain available without a model server' : 'Offline · C++ service is not responding'}</small></span>
            </div>
            {engineOnline && (
              <div className="connection-detail">
                <span className={`status-dot ${engine?.models.length ? 'online' : 'offline'}`} />
                <span><strong>Local model status</strong><small>
                  {engine?.models.length
                    ? `${engine.models.length} model(s) detected${selectedModel ? ` · selected: ${selectedModel}` : ''}`
                    : 'No model API server detected · analysis is unavailable until one is started'}
                </small></span>
              </div>
            )}
            {engineError && !engineOnline && <p className="connection-error" role="status">{engineError}</p>}
            {engineOnline && engine?.diagnostics.map((diagnostic) => (
              <p className="connection-diagnostic" key={diagnostic}>{diagnostic}</p>
            ))}
            {!engineOnline && (
              <div className="command-box">
                <span>.\build\Release\cat-osint.exe --serve</span>
                <small>Start the engine. A local model server is optional; the dashboard and API source settings remain available without one.</small>
              </div>
            )}
            <form className="model-server-form" onSubmit={saveModelServer}>
              <label htmlFor="model-server-url">OpenAI-compatible local server base URL</label>
              <div className="model-server-controls">
                <input
                  id="model-server-url"
                  onChange={(event) => setModelServerDraft(event.target.value)}
                  placeholder="http://127.0.0.1:1234"
                  value={modelServerDraft}
                />
                <button className="button button-secondary" type="submit">Save &amp; check</button>
              </div>
              <small>LM Studio’s API server is usually at port 1234. In LM Studio, start its local server; having a chat open alone does not expose the API. This address is saved in this browser and must be loopback.</small>
              {modelServerError && <p className="form-error" role="alert">{modelServerError}</p>}
            </form>
            <div className="settings-source-summary">
              <div>
                <strong>API sources</strong>
                <small>{remoteSources.length} configured · USGS, NWS, and NASA EONET feeds</small>
              </div>
              <button
                className="button button-secondary"
                onClick={() => {
                  setEngineDialogOpen(false)
                  setSourceDialogOpen(true)
                }}
                type="button"
              >
                Add API
              </button>
            </div>
            <div className="modal-actions">
              <button className="button button-secondary" disabled={isCheckingEngine} onClick={() => setRefreshRevision((revision) => revision + 1)} type="button">
                <Icon name="radio" size={15} /> {isCheckingEngine ? 'Checking…' : 'Check again'}
              </button>
              <button className="button button-primary" onClick={() => setEngineDialogOpen(false)} type="button">Done</button>
            </div>
          </section>
        </div>
      )}
    </div>
  )
}

export default App
