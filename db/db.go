package db

import (
	"github.com/boltdb/bolt"

	"github.com/subutai-io/agent/config"
	"path"
	"time"
	"github.com/subutai-io/agent/lib/fs"
	"github.com/subutai-io/agent/log"
)

type Db struct {
}

var INSTANCE = Db{}

var (
	sshtunnels = []byte("sshtunnels")
	containers = []byte("containers")
	portmap    = []byte("portmap")
	dbPath     = path.Join(config.Agent.DataPrefix, "agent.db")
)

func initDb() {
	if !fs.FileExists(dbPath) {
		//open and close db to create a proper db file
		db, err := bolt.Open(dbPath, 0600, &bolt.Options{ReadOnly: false})
		log.Check(log.ErrorLevel, "Creating database", err)
		db.Close()
	}
}

func openDb(readOnly bool) (*bolt.DB, error) {
	initDb()

	boltDB, err := bolt.Open(dbPath,
		0600, &bolt.Options{Timeout: 15 * time.Second, ReadOnly: readOnly})
	if err != nil {
		return nil, err
	}

	return boltDB, nil
}

// temporary code for tunnel migration >>>
func (i *Db) DelTunEntry(pid string) (err error) {
	var instance *bolt.DB
	if instance, err = openDb(false); err == nil {
		defer instance.Close()
		return instance.Update(func(tx *bolt.Tx) error {
			if b := tx.Bucket(sshtunnels); b != nil {
				return b.DeleteBucket([]byte(pid))
			}
			return nil
		})
	}
	return err
}

func (i *Db) GetTunList() (list []map[string]string, err error) {
	var instance *bolt.DB
	if instance, err = openDb(true); err == nil {
		defer instance.Close()
		instance.View(func(tx *bolt.Tx) error {
			if b := tx.Bucket(sshtunnels); b != nil {
				b.ForEach(func(k, v []byte) error {
					if c := b.Bucket([]byte(k)); c != nil {
						item := make(map[string]string)
						item["pid"] = string(k)
						c.ForEach(func(n, m []byte) error {
							item[string(n)] = string(m)
							return nil
						})
						list = append(list, item)
					}
					return nil
				})
				return nil
			}
			return nil
		})
	}
	return list, err
}

// temporary code for container migration >>>
func (i *Db) RemoveContainer(name string) (err error) {
	var instance *bolt.DB
	if instance, err = openDb(false); err == nil {
		defer instance.Close()
		return instance.Update(func(tx *bolt.Tx) error {
			if b := tx.Bucket(containers); b != nil {
				if err = b.DeleteBucket([]byte(name)); err != nil {
					return err
				}
			}
			return nil
		})
	}
	return err
}

func (i *Db) GetContainers() (list []string, err error) {
	var instance *bolt.DB
	if instance, err = openDb(true); err == nil {
		defer instance.Close()
		instance.View(func(tx *bolt.Tx) error {
			if b := tx.Bucket(containers); b != nil {
				b.ForEach(func(k, v []byte) error {
					list = append(list, string(k))
					return nil
				})
			}
			return nil
		})
	}
	return list, err
}

func (i *Db) GetContainerByName(name string) (c map[string]string, err error) {
	c = make(map[string]string)
	var instance *bolt.DB
	if instance, err = openDb(true); err == nil {
		defer instance.Close()
		instance.View(func(tx *bolt.Tx) error {
			if b := tx.Bucket(containers); b != nil {
				if b = b.Bucket([]byte(name)); b != nil {
					b.ForEach(func(kk, vv []byte) error {
						c[string(kk)] = string(vv)
						return nil
					})
				}
			}
			return nil
		})
	}
	return c, err
}

// temporary code for port mapping migration >>>
type PortMap struct {
	Protocol       string
	ExternalSocket string
	InternalSocket string
	Domain         string
}

func (i *Db) GetAllPortMappings(protocol string) (list []PortMap, err error) {
	var instance *bolt.DB
	if instance, err = openDb(true); err == nil {
		defer instance.Close()
		instance.View(func(tx *bolt.Tx) error {
			if b := tx.Bucket(portmap); b != nil {
				if b = b.Bucket([]byte(protocol)); b != nil {
					b.ForEach(func(k, v []byte) error {
						if c := b.Bucket(k); c != nil {
							c.ForEach(func(kk, vv []byte) error {
								if d := c.Bucket(kk); d != nil {
									d.ForEach(func(kkk, vvv []byte) error {
										if d.Bucket(kkk) != nil {
											mapping := &PortMap{Protocol: protocol, ExternalSocket: string(k), InternalSocket: string(kkk)}
											if protocol == "http" || protocol == "https" {
												mapping.Domain = string(kk)
											}
											list = append(list, *mapping)
										}
										return nil
									})
								}
								return nil
							})
						}
						return nil
					})
				}
			}
			return nil
		})
	}
	return list, err
}

func (i *Db) PortMapDelete(protocol, external, domain, internal string) (left int, err error) {
	var instance *bolt.DB
	if instance, err = openDb(false); err == nil {
		defer instance.Close()
		instance.Update(func(tx *bolt.Tx) error {
			if b := tx.Bucket(portmap); b != nil {
				if b := b.Bucket([]byte(protocol)); b != nil {
					if len(domain) > 0 {
						if b = b.Bucket([]byte(external)); b != nil {
							if len(internal) > 0 {
								if b = b.Bucket([]byte(domain)); b != nil {
									b.DeleteBucket([]byte(internal))
									left = b.Stats().BucketN - 2
								}
							} else {
								b.DeleteBucket([]byte(domain))
								left = b.Stats().BucketN - 2
							}
						}
					} else {
						b.DeleteBucket([]byte(external))
						left = b.Stats().BucketN - 2
					}
				}
			}
			return nil
		})
	}
	return left, err
}

func (i *Db) PortInMap(protocol, external, domain, internal string) (res bool, err error) {
	var instance *bolt.DB
	if instance, err = openDb(true); err == nil {
		defer instance.Close()
		instance.View(func(tx *bolt.Tx) error {
			if b := tx.Bucket(portmap); b != nil {
				if b = b.Bucket([]byte(protocol)); b != nil {
					if b = b.Bucket([]byte(external)); b != nil {
						if len(domain) > 0 {
							if b = b.Bucket([]byte(domain)); b != nil {
								if len(internal) > 0 {
									if b = b.Bucket([]byte(internal)); b != nil {
										res = true
									}
								} else {
									res = true
								}
							}
						} else {
							res = true
						}
					}
				}
			}
			return nil
		})
	}
	return res, err
}
