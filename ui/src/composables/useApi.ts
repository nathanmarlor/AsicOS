const BASE_URL = import.meta.env.VITE_MOCK === '1' ? '' : (import.meta.env.DEV ? 'http://192.168.4.1' : '')

export function useApi() {
  function isJsonResponse(res: Response): boolean {
    const ct = res.headers.get('content-type')
    const cl = res.headers.get('content-length')
    if (cl === '0') return false
    if (ct && ct.includes('application/json')) return true
    return false
  }

  async function get<T = any>(path: string): Promise<T> {
    const res = await fetch(`${BASE_URL}${path}`, {
      headers: { 'Accept': 'application/json' }
    })
    if (!res.ok) throw new Error(`GET ${path}: ${res.status}`)
    if (!isJsonResponse(res)) return null as T
    return res.json()
  }

  async function post<T = any>(path: string, body?: any): Promise<T> {
    const res = await fetch(`${BASE_URL}${path}`, {
      method: 'POST',
      headers: {
        'Content-Type': 'application/json',
        'Accept': 'application/json'
      },
      body: body != null ? JSON.stringify(body) : undefined
    })
    if (!res.ok) throw new Error(`POST ${path}: ${res.status}`)
    if (!isJsonResponse(res)) return null as T
    return res.json()
  }

  return { get, post, BASE_URL }
}
